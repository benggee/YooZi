#pragma once

#include <cstdlib>
#include <string>
#include <stdexcept>

namespace speech {

struct AlibabaConfig {
    std::string appkey;
    std::string token;
    std::string dashscope_api_key;  // DashScope API key for CosyVoice TTS

    static AlibabaConfig from_env() {
        AlibabaConfig cfg;
        const char* ak = std::getenv("ALIBABA_NLS_APPKEY");
        const char* tk = std::getenv("ALIBABA_NLS_TOKEN");
        if (!ak || !tk) {
            throw std::runtime_error(
                "ALIBABA_NLS_APPKEY and ALIBABA_NLS_TOKEN environment variables must be set");
        }
        cfg.appkey = ak;
        cfg.token = tk;

        const char* dsk = std::getenv("DASHSCOPE_API_KEY");
        if (dsk) {
            cfg.dashscope_api_key = dsk;
        }
        return cfg;
    }
};

} // namespace speech
