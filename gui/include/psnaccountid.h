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
                            return base64_encode(responseData.substr(userIdStart, userIdEnd - userIdStart));
                        }
                    }

                    // Return an empty string if access_token is not found
                    return "";
                }

                // Cleanup
                curl_easy_cleanup(curl);
                return nullptr;
            }
        }

    private:
        static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
            size_t totalSize = size * nmemb;
            output->append(static_cast<char*>(contents), totalSize);
            return totalSize;
        }

        static std::string base64_encode(const std::string& input) {
            static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

            std::stringstream encoded;
            size_t i = 0;
            uint32_t buffer = 0;
            int bits_remaining = 0;

            for (char c : input) {
                buffer = (buffer << 8) | static_cast<uint8_t>(c);
                bits_remaining += 8;

                while (bits_remaining >= 6) {
                    bits_remaining -= 6;
                    encoded << base64_chars[(buffer >> bits_remaining) & 0x3F];
                }
            }

            if (bits_remaining > 0) {
                buffer <<= 6 - bits_remaining;
                encoded << base64_chars[buffer & 0x3F];
            }

            // Add padding if necessary
            while (encoded.str().size() % 4 != 0) {
                encoded << "=";
            }

            return encoded.str();
        }
};

#endif //PSNACCOUNTID_H
