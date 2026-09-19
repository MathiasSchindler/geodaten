#include "platform.h"
#include "runtime.h"

#include "crypto/x509.h"
#include "tls/tls12_client.h"
#include "tls/tls13_client.h"

#include <stddef.h>

static const char *macos_tls_error = "none";
static char macos_tls_peer_status_storage[160] = "not-verified-native";
static const char *macos_tls_peer_status = macos_tls_peer_status_storage;

const char *platform_tls_last_error(void) {
    return macos_tls_error;
}

const char *platform_tls_peer_verification_status(void) {
    return macos_tls_peer_status;
}

static void macos_tls_set_error(const char *message) {
    macos_tls_error = message != 0 ? message : "unknown tls error";
}

static void macos_tls_set_peer_status(const char *message) {
    rt_copy_string(macos_tls_peer_status_storage, sizeof(macos_tls_peer_status_storage), message != 0 ? message : "unknown");
}

static unsigned char *macos_tls_read_file(const char *path, size_t *length_out) {
    unsigned char *buffer;
    size_t used = 0U;
    size_t capacity = 4096U;
    int fd;

    if (path == 0 || length_out == 0) {
        return 0;
    }
    fd = platform_open_read(path);
    if (fd < 0) {
        return 0;
    }
    buffer = (unsigned char *)rt_malloc(capacity);
    if (buffer == 0) {
        (void)platform_close(fd);
        return 0;
    }
    for (;;) {
        unsigned char *resized;
        long result = platform_read(fd, buffer + used, capacity - used);
        if (result < 0) {
            rt_free(buffer);
            (void)platform_close(fd);
            return 0;
        }
        if (result == 0) {
            break;
        }
        used += (size_t)result;
        if (used < capacity) {
            continue;
        }
        if (capacity >= 1024U * 1024U) {
            rt_free(buffer);
            (void)platform_close(fd);
            return 0;
        }
        resized = (unsigned char *)rt_realloc(buffer, capacity * 2U);
        if (resized == 0) {
            rt_free(buffer);
            (void)platform_close(fd);
            return 0;
        }
        buffer = resized;
        capacity *= 2U;
    }
    (void)platform_close(fd);
    *length_out = used;
    return buffer;
}

static unsigned char *macos_tls_load_trust_pem(size_t *length_out) {
    unsigned char *buffer;

    buffer = macos_tls_read_file("/etc/ssl/cert.pem", length_out);
    if (buffer != 0) return buffer;
    buffer = macos_tls_read_file("/private/etc/ssl/cert.pem", length_out);
    if (buffer != 0) return buffer;
    buffer = macos_tls_read_file("/opt/homebrew/etc/ca-certificates/cert.pem", length_out);
    if (buffer != 0) return buffer;
    buffer = macos_tls_read_file("/usr/local/etc/ca-certificates/cert.pem", length_out);
    if (buffer != 0) return buffer;
    return 0;
}

static int macos_tls_verify_peer_certs(const CryptoX509DerCert *certs, size_t cert_count, const char *host) {
    unsigned char *trust_pem;
    size_t trust_pem_len = 0U;
    char status[160];
    int result;

    if (cert_count == 0U) {
        macos_tls_set_peer_status("no peer certificate");
        return -1;
    }
    trust_pem = macos_tls_load_trust_pem(&trust_pem_len);
    if (trust_pem == 0) {
        macos_tls_set_peer_status("no trust anchor bundle found");
        return -1;
    }
    status[0] = '\0';
    result = crypto_x509_verify_chain(certs, cert_count, host, platform_get_epoch_time(), trust_pem, trust_pem_len, status, sizeof(status));
    rt_free(trust_pem);
    macos_tls_set_peer_status(result == 0 ? "trusted" : status);
    return result;
}

static int macos_tls_verify_native_peer(Tls13Client *native, const char *host) {
    CryptoX509DerCert certs[TLS13_MAX_PEER_CERTS];
    size_t cert_count;

    cert_count = tls13_client_peer_certificates(native, certs, TLS13_MAX_PEER_CERTS);
    return macos_tls_verify_peer_certs(certs, cert_count, host);
}

static int macos_tls_verify_native12_peer(Tls12Client *native, const char *host) {
    CryptoX509DerCert certs[TLS12_MAX_PEER_CERTS];
    size_t cert_count;

    cert_count = tls12_client_peer_certificates(native, certs, TLS12_MAX_PEER_CERTS);
    return macos_tls_verify_peer_certs(certs, cert_count, host);
}

static Tls13Client *macos_native_tls_client(PlatformTlsClient *client) {
    return (Tls13Client *)client->opaque[0];
}

static Tls12Client *macos_native_tls12_client(PlatformTlsClient *client) {
    return (Tls12Client *)client->opaque[0];
}

static int macos_native_tls_version(PlatformTlsClient *client) {
    return client->opaque[1] == (void *)12 ? 12 : 13;
}

static int macos_native_tls_insecure_opt_in(void) {
    const char *value = platform_getenv("NEWOS_NATIVE_TLS_INSECURE");

    return value != 0 && value[0] == '1' && value[1] == '\0';
}

int platform_tls_peer_info(PlatformTlsClient *client, PlatformTlsPeerInfo *info_out) {
    CryptoX509DerCert certs[TLS13_MAX_PEER_CERTS];
    CryptoX509CertificateInfo cert_info;
    size_t cert_count;

    if (client == 0 || info_out == 0 || !client->active) {
        return -1;
    }
    memset(info_out, 0, sizeof(*info_out));
    if (macos_native_tls_version(client) == 12) {
        cert_count = tls12_client_peer_certificates(macos_native_tls12_client(client), certs, TLS12_MAX_PEER_CERTS);
        rt_copy_string(info_out->protocol, sizeof(info_out->protocol), "TLSv1.2");
        rt_copy_string(info_out->cipher, sizeof(info_out->cipher), "TLS_RSA_WITH_AES_256_GCM_SHA384");
    } else {
        cert_count = tls13_client_peer_certificates(macos_native_tls_client(client), certs, TLS13_MAX_PEER_CERTS);
        rt_copy_string(info_out->protocol, sizeof(info_out->protocol), "TLSv1.3");
        rt_copy_string(info_out->cipher, sizeof(info_out->cipher), "TLS_AES_128_GCM_SHA256");
    }
    rt_copy_string(info_out->verification, sizeof(info_out->verification), platform_tls_peer_verification_status());
    if (cert_count == 0U || crypto_x509_describe_certificate(certs[0].data, certs[0].length, &cert_info) != 0) {
        return -1;
    }
    rt_copy_string(info_out->subject, sizeof(info_out->subject), cert_info.subject);
    rt_copy_string(info_out->issuer, sizeof(info_out->issuer), cert_info.issuer);
    rt_copy_string(info_out->dns_names, sizeof(info_out->dns_names), cert_info.dns_names);
    info_out->not_before = cert_info.not_before;
    info_out->not_after = cert_info.not_after;
    return 0;
}

int platform_tls_connect(PlatformTlsClient *client, const char *host, unsigned int port) {
    Tls13Client *native;
    Tls12Client *native12;
    int socket_fd = -1;

    macos_tls_set_error("none");
    macos_tls_set_peer_status("not-verified-native");
    if (client == 0 || host == 0 || host[0] == '\0') {
        macos_tls_set_error("invalid tls connect arguments");
        return -1;
    }
    memset(client, 0, sizeof(*client));
    client->socket_fd = -1;
    if (platform_connect_tcp(host, port, &socket_fd) != 0) {
        macos_tls_set_error("tcp connect failed");
        return -1;
    }
    native = (Tls13Client *)rt_malloc(sizeof(*native));
    if (native == 0) {
        macos_tls_set_error("native tls state allocation failed");
        (void)platform_close(socket_fd);
        return -1;
    }
    tls13_client_init(native, socket_fd, 30000U);
    if (tls13_client_handshake(native, host, rt_strlen(host)) != 0) {
        macos_tls_set_error(tls13_client_last_error(native));
        rt_free(native);
        (void)platform_close(socket_fd);
        socket_fd = -1;
        if (platform_connect_tcp(host, port, &socket_fd) != 0) {
            macos_tls_set_error("tcp reconnect failed for tls12 fallback");
            return -1;
        }
        native12 = (Tls12Client *)rt_malloc(sizeof(*native12));
        if (native12 == 0) {
            macos_tls_set_error("native tls12 state allocation failed");
            (void)platform_close(socket_fd);
            return -1;
        }
        tls12_client_init(native12, socket_fd, 30000U);
        if (tls12_client_handshake(native12, host, rt_strlen(host)) != 0) {
            macos_tls_set_error(tls12_client_last_error(native12));
            rt_free(native12);
            (void)platform_close(socket_fd);
            return -1;
        }
        if (macos_tls_verify_native12_peer(native12, host) != 0) {
            if (macos_native_tls_insecure_opt_in()) {
                macos_tls_set_error("none");
            } else {
            macos_tls_set_error("certificate verification failed");
            rt_free(native12);
            (void)platform_close(socket_fd);
            return -1;
            }
        }
        client->opaque[0] = native12;
        client->opaque[1] = (void *)12;
        client->socket_fd = socket_fd;
        client->active = 1;
        macos_tls_set_error("none");
        return 0;
    }
    if (macos_tls_verify_native_peer(native, host) != 0) {
        if (macos_native_tls_insecure_opt_in()) {
            macos_tls_set_error("none");
        } else {
        macos_tls_set_error("certificate verification failed");
        rt_free(native);
        (void)platform_close(socket_fd);
        return -1;
        }
    }
    client->opaque[0] = native;
    client->opaque[1] = (void *)13;
    client->socket_fd = socket_fd;
    client->active = 1;
    macos_tls_set_error("none");
    return 0;
}

long platform_tls_read(PlatformTlsClient *client, void *buffer, size_t count) {
    Tls13Client *native;
    long result;

    if (client == 0 || buffer == 0 || count == 0U || !client->active) {
        macos_tls_set_error("invalid tls read arguments");
        return -1;
    }
    native = macos_native_tls_client(client);
    if (macos_native_tls_version(client) == 12) {
        result = tls12_client_read_app(macos_native_tls12_client(client), (unsigned char *)buffer, count);
        if (result < 0) macos_tls_set_error(tls12_client_last_error(macos_native_tls12_client(client)));
        return result;
    }
    result = tls13_client_read_app(native, (unsigned char *)buffer, count);
    if (result < 0) {
        macos_tls_set_error(tls13_client_last_error(native));
    }
    return result;
}

long platform_tls_write(PlatformTlsClient *client, const void *buffer, size_t count) {
    if (client == 0 || buffer == 0 || count == 0U || !client->active) {
        macos_tls_set_error("invalid tls write arguments");
        return -1;
    }
    if (macos_native_tls_version(client) == 12) {
        return tls12_client_write_app(macos_native_tls12_client(client), (const unsigned char *)buffer, count);
    }
    return tls13_client_write_app(macos_native_tls_client(client), (const unsigned char *)buffer, count);
}

void platform_tls_close(PlatformTlsClient *client) {
    Tls13Client *native;

    if (client != 0) {
        native = macos_native_tls_client(client);
        if (native != 0 && macos_native_tls_version(client) == 12) {
            (void)tls12_client_close_notify((Tls12Client *)native);
            rt_free(native);
        } else if (native != 0) {
            (void)tls13_client_close_notify(native);
            rt_free(native);
        }
        if (client->socket_fd >= 0) {
            (void)platform_close(client->socket_fd);
        }
        memset(client, 0, sizeof(*client));
        client->socket_fd = -1;
    }
}
