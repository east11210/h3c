#include <sys/stat.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <wait.h>
#include "eapol.h"
#include "h3c.h"
#include "md5.h"

// H3C descriptions
const struct {
    int code;
    const char *message;
} H3C_DESC[] = {
        {H3C_OK,                   "No error"},
        {H3C_S_EAP_FAILURE,        "EAP Failure"},
        {H3C_S_EAP_SUCCESS,        "EAP Success"},
        {H3C_S_EAP_TYPE_IDENTITY,  "Got EAP Request - Identity"},
        {H3C_S_EAP_TYPE_MD5,       "Got EAP Request - MD5 Challenge"},
        {H3C_S_EAP_TYPE_H3C,       "Got EAP Request - H3C Challenge"},
        {H3C_S_EAP_RESPONSE,       "EAP Response"},
        {H3C_S_EAP_START,          "EAP Auth Start"},
        {H3C_S_EAP_UNKNOWN,        "EAP Unknown"},
        {H3C_E_INVALID_PARAMETERS, "Invalid parameters"},
        {H3C_E_EAPOL_INIT,         "Fail to initialize EAPoL"},
        {H3C_E_EAPOL_START,        "Failed to send EAPoL authentication"},
        {H3C_E_EAPOL_RESPONSE,     "Failed to response EAPoL authentication"},
};

// H3C version information
const static uint8_t VERSION_INFO[32] = {
        0x06, 0x07, 'b', 'j', 'Q', '7', 'S', 'E', '8', 'B', 'Z', '3', 'M', 'q', 'H', 'h',
        's', '3', 'c', 'l', 'M', 'r', 'e', 'g', 'c', 'D', 'Y', '3', 'Y', '=', 0x20, 0x20
};

// H3C context
static h3c_ctx_t *ctx = NULL;

// EAPoL context and callback
static eapol_ctx_t ec = {0};

const char *h3c_desc(int code) {
    return H3C_DESC[code].message;
}


static int h3c_eap_response() {
    ctx->output(H3C_S_EAP_RESPONSE);
    return EAPOL_OK;
}

static int h3c_eap_success() {
    ctx->output(H3C_S_EAP_SUCCESS);
    return EAPOL_OK;
}

static int h3c_eap_failure() {
    ctx->output(H3C_S_EAP_FAILURE);
    return EAPOL_E_AUTH_FAILURE;
}

static int h3c_eap_unknown() {
    ctx->output(H3C_S_EAP_UNKNOWN);
    return EAPOL_OK;
}

static int h3c_send_id(uint8_t *out, uint16_t *length) {
    ctx->output(H3C_S_EAP_TYPE_IDENTITY);
    memcpy(out, VERSION_INFO, sizeof(VERSION_INFO));
    memcpy(out + sizeof(VERSION_INFO), ctx->username, strlen(ctx->username));
    *length = sizeof(VERSION_INFO) + strlen(ctx->username);

    return EAPOL_OK;
}

static int h3c_send_md5(uint8_t id, uint8_t *in, uint8_t *out, uint16_t *length) {
    ctx->output(H3C_S_EAP_TYPE_MD5);
    // MD5(id + password + md5data)
    uint8_t md5[16] = {0};
    uint8_t len = strlen(ctx->password);

    out[0] = id;
    memcpy(out + 1, ctx->password, len);
    memcpy(out + 1 + len, in + 1, in[0]);

    MD5_CTX context;
    MD5_Init(&context);
    MD5_Update(&context, out, 1 + len + in[0]);
    MD5_Final(md5, &context);

    out[0] = 16;
    memcpy(out + 1, md5, sizeof(md5));
    memcpy(out + 1 + sizeof(md5), ctx->username, strlen(ctx->username));
    *length = 1 + sizeof(md5) + strlen(ctx->username);

    return EAPOL_OK;
}

int h3c_init(h3c_ctx_t *c) {
    // Check parameters
    if (c == NULL || c->interface == NULL || c->username == NULL || c->password == NULL || c->output == NULL
        || strlen(c->interface) == 0 || strlen(c->username) == 0 || strlen(c->password) == 0)
        return H3C_E_INVALID_PARAMETERS;
    else
        ctx = c;

    // Set EAPoL context
    ec.interface = ctx->interface;
    ec.response = h3c_eap_response;
    ec.success = h3c_eap_success;
    ec.failure = h3c_eap_failure;
    ec.unknown = h3c_eap_unknown;
    ec.id = h3c_send_id;
    ec.md5 = h3c_send_md5;

    if (eapol_init(&ec) != EAPOL_OK)
        return H3C_E_EAPOL_INIT;

    return H3C_OK;
}

void h3c_cleanup() {
    eapol_logoff();
    eapol_cleanup();
}

static void h3c_signal_handler() {
    h3c_cleanup();
    exit(EXIT_SUCCESS);
}

void h3c_run() {
    ctx->output(H3C_S_EAP_START);

    if (eapol_start() != EAPOL_OK) {
        ctx->output(H3C_E_EAPOL_START);
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, h3c_signal_handler);
    signal(SIGTERM, h3c_signal_handler);

    while (1) {
        if (eapol_dispatcher() != EAPOL_OK) {
            exit(EXIT_FAILURE);
        }
    }
}

void h3c_daemon() {
    if (getpid() == 1)
        return;

    pid_t pid = fork();

    if (pid < 0)
        exit(EXIT_FAILURE);

    if (pid > 0)
        exit(EXIT_SUCCESS);

    pid_t sid = setsid();

    if (sid < 0)
        exit(EXIT_FAILURE);

    if ((chdir("/")) < 0)
        exit(EXIT_FAILURE);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    umask(0);
    openlog("h3c", LOG_CONS, LOG_USER);
    syslog(LOG_INFO, "Start daemon");

    while (true) {
        pid = fork();
        if (0 == pid) {
            h3c_run();
            exit(EXIT_SUCCESS);
        } else if (0 < pid) {
            int status = 0;
            pid_t wait_pid;
            syslog(LOG_INFO, "Start child %d\n", pid);
            do {
                wait_pid = waitpid(pid, &status, 0);
                syslog(LOG_INFO, "Child %d exited, will restart later!\n", wait_pid);
            } while (wait_pid != pid && -1 != wait_pid);
        }
        sleep(5);
    }
}
