#pragma once

#include <HTTPClient.h>
#include <vector>

struct HttpResult {
    bool success;              // Whether the request succeeded
    int statusCode;            // HTTP status code (0 if network error)
    String response;           // Response body (empty on error)
    String errorMessage;       // Error description if success == false
    String rateLimitRemaining; // X-Rate-Limit-Remaining, empty if the server sent none
};

class HttpRequestManager
{
private:
    HTTPClient http;

    String BuildQueryString(const std::vector<std::pair<String, String>>& params) const;

public:
    HttpRequestManager() = default;
    ~HttpRequestManager() = default;

    [[nodiscard]] HttpResult Get(const String& url, const std::vector<std::pair<String, String>>& params = {}, const std::vector<std::pair<String, String>>& headers = {});
    [[nodiscard]] HttpResult Post(const String& url, const String& body = "", const std::vector<std::pair<String, String>>& headers = {});

    // Binary-safe GET into a PSRAM buffer, for map tiles: HttpResult::response is
    // a String and would cut a PNG off at its first null byte.
    //
    // On success *outData holds *outLen bytes and belongs to the caller, who
    // releases it with free(). On failure both stay untouched at nullptr / 0.
    //
    // The user agent is not optional: the OSM tile policy rejects library
    // defaults, so callers have to identify themselves.
    [[nodiscard]] HttpResult GetToBuffer(const String& url, uint8_t** outData, size_t* outLen, const String& userAgent);
};