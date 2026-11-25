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
        xTaskCreatePinnedToCore(voiceTask, "nxt_voice", 8192, this, 4, nullptr, 1);
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
        const char* text;
    };

    static constexpr Phrase kPhrases[] = {
        {NxtVoiceCmd::Forward, "go forward"},
        {NxtVoiceCmd::Forward, "move forward"},
        {NxtVoiceCmd::Forward, "go straight"},
        {NxtVoiceCmd::Back, "go back"},
        {NxtVoiceCmd::Back, "move back"},
        {NxtVoiceCmd::Back, "go backward"},
        {NxtVoiceCmd::Left, "turn left"},
        {NxtVoiceCmd::Right, "turn right"},
        {NxtVoiceCmd::Stop, "stop"},
        {NxtVoiceCmd::Stop, "stop now"},
        {NxtVoiceCmd::Stop, "halt"},
        {NxtVoiceCmd::Faster, "faster"},
        {NxtVoiceCmd::Faster, "speed up"},
        {NxtVoiceCmd::Slower, "slower"},
        {NxtVoiceCmd::Slower, "slow down"},
        {NxtVoiceCmd::Beep, "beep"},
        {NxtVoiceCmd::Beep, "make a sound"},
        {NxtVoiceCmd::SpinAround, "spin around"},
        {NxtVoiceCmd::SpinAround, "turn around"},
    };

    static void voiceTask(void* arg)
    {
        auto* self = static_cast<NxtVoice*>(arg);
        self->run();
        vTaskDelete(nullptr);
    }

    void run()
    {
        srmodel_list_t* models = esp_srmodel_init("nxtvoice");
        if (!models || models->num <= 0) {
            setStatus("no voice model");
            ESP_LOGE(TAG, "nxtvoice partition has no models");
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

        esp_err_t err = esp_mn_commands_alloc(mn, mn_data);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            setStatus("cmd alloc err");
            return;
        }
        esp_mn_commands_clear();
        for (int i = 0; i < (int)(sizeof(kPhrases) / sizeof(kPhrases[0])); ++i) {
            // phrase index + 1 as command id so we can map back to exact phrase
            esp_mn_commands_add(i + 1, kPhrases[i].text);
        }
        esp_mn_error_t* mn_err = esp_mn_commands_update();
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
