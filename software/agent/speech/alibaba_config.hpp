#pragma once

#include <cstdlib>
#include <string>
#include <stdexcept>

namespace speech {

struct AlibabaConfig {
    std::string appkey;
    std::string token;

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
        return cfg;
    }
};

} // namespace speech
