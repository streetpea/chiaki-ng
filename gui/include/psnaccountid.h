//
// Based on script by grill2010
//

#ifndef PSNACCOUNTID_H
#define PSNACCOUNTID_H
#include <iostream>
#include <ostream>
#include <sstream>
#include <curl/curl.h>

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
        static std::string getAccessToken(std::string code) {
            // Build the request body
            std::string body = "grant_type=authorization_code&code=" + code + "&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&";

            // Initialize cURL
            CURL* curl = curl_easy_init();
            if (curl) {
                // Set the request URL
                curl_easy_setopt(curl, CURLOPT_URL, PSNAuth::TOKEN_URL.c_str());

                // Set HTTP Basic Authentication
                std::string authString = PSNAuth::CLIENT_ID + ":" + PSNAuth::CLIENT_SECRET;
                curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
                curl_easy_setopt(curl, CURLOPT_USERPWD, authString.c_str());

                // Set the request headers
                struct curl_slist* headers = NULL;
                headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

                // Set the request body
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());

                // Set the callback function to handle the response
                std::string responseData;
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PSNAccountID::WriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);

                // Perform the HTTP POST request
                CURLcode res = curl_easy_perform(curl);

                // Check for errors
                if (res != CURLE_OK) {
                    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                } else {
                    std::string accessTokenKey = R"("access_token":")";
                    size_t accessTokenStart = responseData.find(accessTokenKey);

                    if (accessTokenStart != std::string::npos) {
                        accessTokenStart += accessTokenKey.length();
                        size_t accessTokenEnd = responseData.find('\"', accessTokenStart);
                        if (accessTokenEnd != std::string::npos) {
                            return responseData.substr(accessTokenStart, accessTokenEnd - accessTokenStart);
                        }
                    }

                    // Return an empty string if access_token is not found
                    return "";
                }

                // Cleanup
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
            }
            return nullptr;
        }

        static std::string GetAccountInfo(std::string token) {
            // Build the request URL for account info
            std::string accountInfoUrl = PSNAuth::TOKEN_URL + "/" + curl_easy_escape(nullptr, token.c_str(), token.length());

            // Initialize cURL
            CURL* curl = curl_easy_init();
            if (curl) {
                // Set the request URL
                curl_easy_setopt(curl, CURLOPT_URL, accountInfoUrl.c_str());

                // Set HTTP Basic Authentication
                std::string authString = PSNAuth::CLIENT_ID + ":" + PSNAuth::CLIENT_SECRET;
                curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
                curl_easy_setopt(curl, CURLOPT_USERPWD, authString.c_str());

                // Set the callback function to handle the response
                std::string responseData;
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);

                // Perform the HTTP GET request
                CURLcode res = curl_easy_perform(curl);

                // Check for errors
                if (res != CURLE_OK) {
                    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                } else {
                    std::string userIdKey = R"("user_id":")";
                    size_t userIdStart = responseData.find(userIdKey);

                    if (userIdStart != std::string::npos) {
                        userIdStart += userIdKey.length();
                        size_t userIdEnd = responseData.find('\"', userIdStart);
                        if (userIdEnd != std::string::npos) {
                            std::string user_id = responseData.substr(userIdStart, userIdEnd - userIdStart);
                            std::vector<unsigned char> byte_representation = to_bytes(std::stoll(user_id), 8, false);

                            return base64_encode(byte_representation);
                        }
                    }

                    // Return an empty string if access_token is not found
                    return "";
                }

                // Cleanup
                curl_easy_cleanup(curl);
            }
            return nullptr;
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

        static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
            size_t totalSize = size * nmemb;
            output->append(static_cast<char*>(contents), totalSize);
            return totalSize;
        }
};

#endif //PSNACCOUNTID_H
