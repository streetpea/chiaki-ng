//
// Based on script by grill2010
//

#ifndef PSNACCOUNTID_H
#define PSNACCOUNTID_H
#include <iostream>
#include <ostream>
#include <sstream>
#include <curl/curl.h>

#include "jsonutils.h"

namespace PSNAuth {
    static const std::string CLIENT_ID = "ba495a24-818c-472b-b12d-ff231c1b5745";
    static const std::string CLIENT_SECRET = "mvaiZkRsAsI1IBkY";
    static const std::string LOGIN_URL = "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize?service_entity=urn:service-entity:psn&response_type=code&client_id=" +
                                         CLIENT_ID + "&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&scope=psn:clientapp&request_locale=en_US&ui=pr&service_logo=ps&layout_type=popup&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal&";
    static const std::string TOKEN_URL = "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token";
    static const std::string REDIRECT_PAGE = "https://remoteplay.dl.playstation.net/remoteplay/redirect";

}

class PSNAccountID {
    public:
        static std::string getAccessToken(ChiakiLog* log, std::string code) {
            // Build the request body
            std::string body = "grant_type=authorization_code&code=" + code + "&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&";

            return JsonUtils::postJsonAttributeBasic(log, PSNAuth::TOKEN_URL, PSNAuth::CLIENT_ID, PSNAuth::CLIENT_SECRET, "access_token", body, "application/x-www-form-urlencoded");
        }

        static std::string GetAccountInfo(ChiakiLog* log, std::string token) {
            // Build the request URL for account info
            std::string accountInfoUrl = PSNAuth::TOKEN_URL + "/" + token.c_str();

            std::string user_id = JsonUtils::getJsonAttributeBasic(log, accountInfoUrl, PSNAuth::CLIENT_ID, PSNAuth::CLIENT_SECRET, "user_id");
            QByteArray byte_representation = to_bytes(std::stoll(user_id), 8);

            return byte_representation.toBase64().toStdString();
        }

    private:

    static QByteArray to_bytes(long number, int num_bytes) {
        std::vector<unsigned char> result(num_bytes);
        for (int i = 0; i < num_bytes; ++i) {
            result[i] = static_cast<unsigned char>(number & 0xFF);
            number >>= 8;
        }
        QByteArray byte_array(reinterpret_cast<const char*>(result.data()), static_cast<int>(result.size()));
        return byte_array;
    }
};

#endif //PSNACCOUNTID_H
