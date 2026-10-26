#include "HttpRequestManager.h"

#include <algorithm>

#include <esp_heap_caps.h>

namespace
{
    constexpr uint32_t BinaryConnectTimeoutMs = 8000;
    constexpr uint32_t BinaryStallTimeoutMs = 8000;

    // Growth step and cap for responses that arrive without a Content-Length.
    // A map tile is 10 to 40 KB, so the cap only guards against a runaway body.
    constexpr size_t BinaryChunkSize = 8192;
    constexpr size_t BinaryMaxSize = 512 * 1024;
}

String HttpRequestManager::BuildQueryString(const std::vector<std::pair<String, String>>& params) const
{
    if (params.empty())
        return "";

    String queryStream = "?";

    bool first = true;
    for (const auto& [key, value] : params)
    {
        if (!first)
            queryStream += "&";

        queryStream += key + "=" + value;

        first = false;
    }

    return queryStream;
}

HttpResult HttpRequestManager::Get(const String& url, const std::vector<std::pair<String, String>>& params, const std::vector<std::pair<String, String>>& headers) {
    HttpResult result{ false, 0, "", "", "" };

    const String queryParams = BuildQueryString(params);
    const String fullUrl = url + queryParams;

    http.begin(fullUrl);

    // add headers to request
    for (const auto& header : headers) {
        http.addHeader(header.first, header.second);
    }

    // OpenSky puts the remaining credit balance here. Other APIs ignore it.
    const char *rateLimitHeader[] = {"X-Rate-Limit-Remaining"};
    http.collectHeaders(rateLimitHeader, 1);

    // send request and handle response
    int responseCode = http.GET();
    result.statusCode = responseCode;
    result.rateLimitRemaining = http.header("X-Rate-Limit-Remaining");

    if (responseCode > 0) {
        result.success = true;
        result.response = http.getString();
    }
    else {
        result.success = false;
        result.errorMessage = http.errorToString(responseCode);
        Serial.print("[GET] HTTP Error (");
        Serial.print(responseCode);
        Serial.print("): ");
        Serial.println(result.errorMessage);
    }

    http.end();
    return result;
}

HttpResult HttpRequestManager::GetToBuffer(const String& url, uint8_t** outData, size_t* outLen, const String& userAgent)
{
    HttpResult result{ false, 0, "", "" };

    if (outData == nullptr || outLen == nullptr)
    {
        result.errorMessage = "outData and outLen are required";
        return result;
    }

    *outData = nullptr;
    *outLen = 0;

    // Deliberately not the shared member: a tile request needs its own user
    // agent and timeouts, and HTTPClient keeps both across requests.
    HTTPClient tileHttp;

    tileHttp.begin(url);
    tileHttp.setUserAgent(userAgent);
    tileHttp.setConnectTimeout(BinaryConnectTimeoutMs);
    tileHttp.setTimeout(BinaryStallTimeoutMs);
    tileHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    const int responseCode = tileHttp.GET();
    result.statusCode = responseCode;

    if (responseCode != HTTP_CODE_OK)
    {
        result.errorMessage = responseCode > 0
            ? "HTTP " + String(responseCode)
            : tileHttp.errorToString(responseCode);

        Serial.printf("[GETBIN] %s failed: %s\n", url.c_str(), result.errorMessage.c_str());

        tileHttp.end();
        return result;
    }

    const int contentLength = tileHttp.getSize();

    if (contentLength > static_cast<int>(BinaryMaxSize))
    {
        result.errorMessage = "body of " + String(contentLength) + " bytes exceeds the limit";
        Serial.printf("[GETBIN] %s failed: %s\n", url.c_str(), result.errorMessage.c_str());

        tileHttp.end();
        return result;
    }

    // Chunked responses carry no length, so start at one chunk and grow.
    size_t capacity = contentLength > 0 ? static_cast<size_t>(contentLength) : BinaryChunkSize;

    uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM));

    if (buffer == nullptr)
    {
        result.errorMessage = "no PSRAM for " + String(capacity) + " bytes";
        Serial.printf("[GETBIN] %s failed: %s\n", url.c_str(), result.errorMessage.c_str());

        tileHttp.end();
        return result;
    }

    WiFiClient* stream = tileHttp.getStreamPtr();

    size_t received = 0;
    uint32_t lastProgress = millis();

    while (contentLength < 0 || received < static_cast<size_t>(contentLength))
    {
        const int available = stream->available();

        if (available > 0)
        {
            if (received == capacity)
            {
                if (capacity >= BinaryMaxSize)
                {
                    result.errorMessage = "body exceeds " + String(BinaryMaxSize) + " bytes";
                    break;
                }

                const size_t grownCapacity = std::min(capacity + BinaryChunkSize, BinaryMaxSize);
                auto* grown = static_cast<uint8_t*>(heap_caps_realloc(buffer, grownCapacity, MALLOC_CAP_SPIRAM));

                if (grown == nullptr)
                {
                    result.errorMessage = "no PSRAM to grow to " + String(grownCapacity) + " bytes";
                    break;
                }

                buffer = grown;
                capacity = grownCapacity;
            }

            // read() fills as much as is buffered; readBytes() would go byte by
            // byte in the Arduino core and take about twice as long.
            const int read = stream->read(buffer + received, std::min(static_cast<size_t>(available), capacity - received));

            if (read > 0)
            {
                received += static_cast<size_t>(read);
                lastProgress = millis();
                continue;
            }
        }

        // connected() also reports true while data is still buffered, so this
        // only trips once the server closed and the buffer ran dry.
        if (!tileHttp.connected())
            break;

        if (millis() - lastProgress > BinaryStallTimeoutMs)
        {
            result.errorMessage = "stalled after " + String(received) + " bytes";
            break;
        }

        delay(1);
    }

    tileHttp.end();

    if (result.errorMessage.isEmpty() && contentLength > 0 && received != static_cast<size_t>(contentLength))
        result.errorMessage = "incomplete body: " + String(received) + " of " + String(contentLength) + " bytes";

    if (result.errorMessage.isEmpty() && received == 0)
        result.errorMessage = "empty body";

    if (!result.errorMessage.isEmpty())
    {
        free(buffer);
        Serial.printf("[GETBIN] %s failed: %s\n", url.c_str(), result.errorMessage.c_str());

        return result;
    }

    *outData = buffer;
    *outLen = received;
    result.success = true;

    return result;
}

HttpResult HttpRequestManager::Post(const String& url, const String& body, const std::vector<std::pair<String, String>>& headers)
{
    HttpResult result{ false, 0, "", "", "" };

    http.begin(url);

    // add headers to request
    for (const auto& header : headers) {
        http.addHeader(header.first, header.second);
    }

    // send request and handle response
    int responseCode = http.POST(body);
    result.statusCode = responseCode;

    if (responseCode > 0) {
        result.success = true;
        result.response = http.getString();
    }
    else {
        result.success = false;
        result.errorMessage = http.errorToString(responseCode);
        Serial.print("[POST] HTTP Error (");
        Serial.print(responseCode);
        Serial.print("): ");
        Serial.println(result.errorMessage);
    }

    http.end();
    return result;
}
