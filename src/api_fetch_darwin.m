/*
 * api_fetch_darwin.m - LightShell fetch() HTTP API (macOS)
 *
 * Provides a global fetch(url) function that performs a synchronous HTTP GET
 * and returns the response body as a string.
 *
 * This is a simplified v1 implementation. A full implementation would return
 * a Response object with .json(), .text() methods and be async/Promise-based.
 */

#import <Foundation/Foundation.h>
#include "api.h"

/* fetch(url) → string (synchronous, v1) */
static R8EValue api_fetch(R8EContext *ctx, R8EValue this_val,
                           int argc, const R8EValue *argv) {
    (void)this_val;

    if (argc < 1 || !r8e_is_string(argv[0])) {
        r8e_throw_type_error(ctx, "fetch: url must be a string");
        return R8E_UNDEFINED;
    }

    /* Extract URL string */
    char url_buf[2048];
    size_t url_len;
    const char *url_raw = r8e_get_cstring(argv[0], url_buf, &url_len);

    /* Make a null-terminated copy */
    char url_str[2048];
    if (url_len >= sizeof(url_str)) url_len = sizeof(url_str) - 1;
    memcpy(url_str, url_raw, url_len);
    url_str[url_len] = '\0';

    /* Build NSURL */
    NSURL *nsurl = [NSURL URLWithString:[NSString stringWithUTF8String:url_str]];
    if (!nsurl) {
        r8e_throw_error(ctx, "fetch: invalid URL '%s'", url_str);
        return R8E_UNDEFINED;
    }

    /* Synchronous request using NSURLSession semaphore pattern */
    __block NSData *responseData = nil;
    __block NSError *responseError = nil;
    __block NSHTTPURLResponse *httpResponse = nil;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    NSURLSession *session = [NSURLSession sharedSession];
    NSURLRequest *request = [NSURLRequest requestWithURL:nsurl
                                            cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                        timeoutInterval:30.0];

    NSURLSessionDataTask *task = [session dataTaskWithRequest:request
                                           completionHandler:^(NSData *data,
                                                               NSURLResponse *response,
                                                               NSError *error) {
        responseData = data;
        responseError = error;
        if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
            httpResponse = (NSHTTPURLResponse *)response;
        }
        dispatch_semaphore_signal(sem);
    }];

    [task resume];
    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

    if (responseError || !responseData) {
        const char *errMsg = responseError
            ? [[responseError localizedDescription] UTF8String]
            : "unknown error";
        r8e_throw_error(ctx, "fetch: request failed: %s", errMsg);
        return R8E_UNDEFINED;
    }

    /* Convert response body to string */
    NSString *body = [[NSString alloc] initWithData:responseData
                                           encoding:NSUTF8StringEncoding];
    if (!body) {
        /* Try Latin-1 as fallback */
        body = [[NSString alloc] initWithData:responseData
                                     encoding:NSISOLatin1StringEncoding];
    }
    if (!body) {
        r8e_throw_error(ctx, "fetch: could not decode response as text");
        return R8E_UNDEFINED;
    }

    return r8e_make_cstring(ctx, [body UTF8String]);
}

void ls_api_fetch_init(R8EContext *ctx) {
    r8e_set_global_func(ctx, "fetch", api_fetch, 1);
}
