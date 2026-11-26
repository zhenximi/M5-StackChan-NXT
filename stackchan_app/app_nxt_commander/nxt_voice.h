#pragma once
// Offline English voice commands for NXT.CMD using ESP-SR MultiNet7 (mn7_en).
// Model is packed into the dedicated "nxtvoice" flash partition (see partitions.csv).
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <model_path.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
extern "C" {
#include <flite_g2p.h>
}
#include <esp_ae_rate_cvt.h>
#include <board.h>
#include <audio/audio_codec.h>

enum class NxtVoiceCmd : int {
    None = 0,
    Forward,
    Back,
    Left,
    Right,
    Stop,
    Faster,
    Slower,
    Beep,
    SpinAround,
};

class NxtVoice {
public:
    using Callback = void (*)(NxtVoiceCmd cmd, const char* phrase, void* ctx);

    static NxtVoice& instance()
    {
        static NxtVoice inst;
        return inst;
    }

    void begin(Callback cb, void* ctx)
    {
        if (_started) return;
        _started = true;
        _cb      = cb;
        _ctx     = ctx;
        _running = true;
        setStatus("loading model...");
        xTaskCreatePinnedToCore(voiceTask, "nxt_voice", 16384, this, 4, nullptr, 1);
    }

    void stop() { _running = false; }

    const char* status() const { return _status; }

private:
    static constexpr const char* TAG = "NxtVoice";
    NxtVoice() = default;

    bool _started            = false;
    std::atomic<bool> _running{false};
    Callback _cb             = nullptr;
    void* _ctx               = nullptr;
    char _status[48]         = "off";

    void setStatus(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        vsnprintf(_status, sizeof(_status), fmt, args);
        va_end(args);
    }

    struct Phrase {
        NxtVoiceCmd cmd;
        const char* upper;
        const char* text;
    };

    static constexpr Phrase kPhrases[] = {
        {NxtVoiceCmd::Forward, "GO FORWARD", "go forward"},
        {NxtVoiceCmd::Forward, "MOVE FORWARD", "move forward"},
        {NxtVoiceCmd::Forward, "GO STRAIGHT", "go straight"},
        {NxtVoiceCmd::Back, "GO BACK", "go back"},
        {NxtVoiceCmd::Back, "MOVE BACK", "move back"},
        {NxtVoiceCmd::Back, "GO BACKWARD", "go backward"},
        {NxtVoiceCmd::Left, "TURN LEFT", "turn left"},
        {NxtVoiceCmd::Right, "TURN RIGHT", "turn right"},
        {NxtVoiceCmd::Stop, "STOP", "stop"},
        {NxtVoiceCmd::Stop, "STOP NOW", "stop now"},
        {NxtVoiceCmd::Stop, "HALT", "halt"},
        {NxtVoiceCmd::Faster, "FASTER", "faster"},
        {NxtVoiceCmd::Faster, "SPEED UP", "speed up"},
        {NxtVoiceCmd::Slower, "SLOWER", "slower"},
        {NxtVoiceCmd::Slower, "SLOW DOWN", "slow down"},
        {NxtVoiceCmd::Beep, "BEEP", "beep"},
        {NxtVoiceCmd::Beep, "MAKE A SOUND", "make a sound"},
        {NxtVoiceCmd::SpinAround, "SPIN AROUND", "spin around"},
        {NxtVoiceCmd::SpinAround, "TURN AROUND", "turn around"},
    };

    static void voiceTask(void* arg)
    {
        auto* self = static_cast<NxtVoice*>(arg);
        self->run();
        vTaskDelete(nullptr);
    }

    void run()
    {
        // esp-sr keeps one global model list; if something else (e.g. assets srmodels) loaded it
        // first, esp_srmodel_init() would return that list instead of mapping our partition.
        srmodel_list_t* models = get_static_srmodels();
        if (models && !esp_srmodel_filter(models, ESP_MN_PREFIX, nullptr)) {
            ESP_LOGW(TAG, "static srmodels lacks multinet; remapping nxtvoice");
            esp_srmodel_deinit(models);
            models = nullptr;
        }
        if (!models) models = esp_srmodel_init("nxtvoice");
        if (!models || models->num <= 0) {
            setStatus("no voice model");
            ESP_LOGE(TAG, "nxtvoice partition has no models");
            return;
        }
        if (!esp_srmodel_filter(models, "fst", nullptr)) {
            setStatus("model pkg missing fst");
            ESP_LOGE(TAG, "srmodels missing 'fst' (required by MultiNet7)");
            return;
        }
        char* mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
        if (!mn_name) mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, nullptr);
        if (!mn_name) {
            setStatus("no multinet");
            return;
        }

        esp_mn_iface_t* mn          = esp_mn_handle_from_name(mn_name);
        model_iface_data_t* mn_data = mn->create(mn_name, 6000);
        if (!mn_data) {
            setStatus("mn create failed");
            return;
        }
        mn->set_det_threshold(mn_data, 0.35f);

        // MultiNet7 English requires G2P phonemes on every phrase node.
        // Because CONFIG_SR_MN_EN_MULTINET7_QUANT is not set in sdkconfig (we pack mn7_en into
        // our own partition), esp_mn_commands_add() skips flite_g2p() and leaves phonemes NULL.
        // Build the esp_mn_node_t list with flite_g2p() directly and pass to set_speech_commands().
        auto* root   = static_cast<esp_mn_node_t*>(calloc(1, sizeof(esp_mn_node_t)));
        auto* tail   = root;
        int added    = 0;
        for (int i = 0; i < (int)(sizeof(kPhrases) / sizeof(kPhrases[0])); ++i) {
            char* ph = flite_g2p(kPhrases[i].upper, 1);
            if (!ph) continue;
            if (mn->check_speech_command(mn_data, ph) == 0) {
                ESP_LOGW(TAG, "invalid phonemes for '%s': '%s'", kPhrases[i].upper, ph);
                free(ph);
                continue;
            }
            auto* p       = static_cast<esp_mn_phrase_t*>(calloc(1, sizeof(esp_mn_phrase_t)));
            p->string     = strdup(kPhrases[i].upper);
            p->phonemes   = ph;
            p->command_id = i + 1;
            p->threshold  = 0;
            auto* node    = static_cast<esp_mn_node_t*>(calloc(1, sizeof(esp_mn_node_t)));
            node->phrase  = p;
            tail->next    = node;
            tail          = node;
            ++added;
            ESP_LOGI(TAG, "added cmd[%d] '%s' -> '%s'", i + 1, kPhrases[i].upper, ph);
        }
        if (added == 0) {
            setStatus("0 valid cmds");
            ESP_LOGE(TAG, "no valid speech commands could be added");
            return;
        }
        esp_mn_error_t* mn_err = mn->set_speech_commands(mn_data, root);
        if (mn_err && mn_err->num > 0) {
            for (int i = 0; i < mn_err->num; ++i) {
                ESP_LOGW(TAG, "rejected phrase: %s", mn_err->phrases[i]->string);
            }
        }

        const int chunk = mn->get_samp_chunksize(mn_data);  // samples @16k
        ESP_LOGI(TAG, "MultiNet %s ready, chunk=%d", mn_name, chunk);

        auto& board = Board::GetInstance();
        auto* codec = board.GetAudioCodec();
        if (!codec) {
            setStatus("no codec");
            return;
        }
        const int in_ch   = codec->input_channels() > 0 ? codec->input_channels() : 1;
        const int in_rate = codec->input_sample_rate();
        codec->EnableInput(true);

        esp_ae_rate_cvt_handle_t rs = nullptr;
        if (in_rate != 16000) {
            esp_ae_rate_cvt_cfg_t cfg = {};
            cfg.src_rate              = (uint32_t)in_rate;
            cfg.dest_rate             = 16000;
            cfg.channel               = 1;
            cfg.bits_per_sample       = 16;
            cfg.complexity            = 2;
            cfg.perf_type             = ESP_AE_RATE_CVT_PERF_TYPE_SPEED;
            esp_ae_rate_cvt_open(&cfg, &rs);
            if (!rs) {
                setStatus("resampler err");
                return;
            }
        }

        const int in_frames = in_rate / 50;  // 20 ms
        std::vector<int16_t> raw(in_frames * in_ch);
        std::vector<int16_t> mono(in_frames);
        std::vector<int16_t> rs_out(in_frames + 32);
        std::vector<int16_t> acc;
        acc.reserve(chunk * 3);

        setStatus("listening");
        while (_running) {
            if (!codec->InputData(raw)) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            for (int i = 0; i < in_frames; ++i) mono[i] = raw[i * in_ch];  // ch0 = mic

            if (rs) {
                uint32_t out_n = rs_out.size();
                esp_ae_rate_cvt_process(rs, (esp_ae_sample_t)mono.data(), in_frames,
                                        (esp_ae_sample_t)rs_out.data(), &out_n);
                acc.insert(acc.end(), rs_out.begin(), rs_out.begin() + out_n);
            } else {
                acc.insert(acc.end(), mono.begin(), mono.end());
            }

            while ((int)acc.size() >= chunk) {
                esp_mn_state_t st = mn->detect(mn_data, acc.data());
                acc.erase(acc.begin(), acc.begin() + chunk);

                if (st == ESP_MN_STATE_DETECTED) {
                    esp_mn_results_t* r = mn->get_results(mn_data);
                    if (r && r->num > 0) {
                        int id = r->command_id[0];
                        if (id >= 1 && id <= (int)(sizeof(kPhrases) / sizeof(kPhrases[0]))) {
                            const Phrase& p = kPhrases[id - 1];
                            ESP_LOGI(TAG, "heard '%s' (p=%.2f)", p.text, r->prob[0]);
                            setStatus("heard: %s", p.text);
                            if (_cb) _cb(p.cmd, p.text, _ctx);
                        }
                    }
                    mn->clean(mn_data);
                } else if (st == ESP_MN_STATE_TIMEOUT) {
                    mn->clean(mn_data);
                }
            }
        }

        codec->EnableInput(false);
        if (rs) esp_ae_rate_cvt_close(rs);
        mn->destroy(mn_data);
        setStatus("off");
    }
};
