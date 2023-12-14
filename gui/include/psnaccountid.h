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
            std::vector<unsigned char> byte_representation = to_bytes(std::stoll(user_id), 8, false);

            return base64_encode(byte_representation);
        }

    private:

    static std::vector<unsigned char> to_bytes(long number, int num_bytes, bool big_endian = true) {
        std::vector<unsigned char> result(num_bytes);

        if (big_endian) {
            for (int i = num_bytes - 1; i >= 0; --i) {
                result[i] = static_cast<unsigned char>(number & 0xFF);
                number >>= 8;
            }
        } else {
            for (int i = 0; i < num_bytes; ++i) {
                result[i] = static_cast<unsigned char>(number & 0xFF);
                number >>= 8;
            }
        }

        return result;
    }

    static std::string base64_encode(const std::vector<unsigned char>& input) {
        const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string result;
        size_t i = 0;

        while (i < input.size()) {
            unsigned char char1 = input[i++];
            unsigned char char2 = (i < input.size()) ? input[i++] : 0;
            unsigned char char3 = (i < input.size()) ? input[i++] : 0;

            unsigned char enc1 = char1 >> 2;
            unsigned char enc2 = ((char1 & 0x03) << 4) | (char2 >> 4);
            unsigned char enc3 = ((char2 & 0x0F) << 2) | (char3 >> 6);
            unsigned char enc4 = char3 & 0x3F;

            result += base64_chars[enc1];
            result += base64_chars[enc2];
            result += (char2 != 0) ? base64_chars[enc3] : '=';
            result += (char3 != 0) ? base64_chars[enc4] : '=';
        }

        return result;
    }
};

#endif //PSNACCOUNTID_H
