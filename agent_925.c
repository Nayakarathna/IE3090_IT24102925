#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PORT 9410
#define SID "5292"
#define TOKEN "OPS-2925"
#define MAX_LINE 4096
#define MAX_FILE (10 * 1024 * 1024)

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static void log_event(const char *fmt, ...) {
    FILE *f = fopen("remoteops_IT24102925.log", "a");
    if (!f) return;
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
    pthread_mutex_lock(&log_lock);
    fprintf(f, "[%s] ", ts);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    pthread_mutex_unlock(&log_lock);
    fclose(f);
}

/* Read exactly one newline-terminated line without consuming following file bytes. */
static ssize_t read_line(int fd, char *buf, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0) return n ? (ssize_t)n : 0;
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (c == '\n') break;
        if (c != '\r') buf[n++] = c;
    }
    buf[n] = '\0';
    return (ssize_t)n;
}
static int send_all(int fd, const void *data, size_t len) {
    const char *p = data;
    while (len) {
        ssize_t n = send(fd, p, len, 0);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return -1;
        p += n; len -= (size_t)n;
    }
    return 0;
}
static int recv_all(int fd, void *data, size_t len) {
    char *p = data;
    while (len) {
        ssize_t n = recv(fd, p, len, 0);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return -1;
        p += n; len -= (size_t)n;
    }
    return 0;
}
static void respond(int fd, const char *fmt, ...) {
    char body[MAX_LINE];
    va_list ap; va_start(ap, fmt); vsnprintf(body, sizeof(body), fmt, ap); va_end(ap);
    char out[MAX_LINE + 32];
    snprintf(out, sizeof(out), "%s SID:%s\n", body, SID);
    (void)send_all(fd, out, strlen(out));
}
static int safe_name(const char *s) {
    return s && *s && !strstr(s, "..") && !strchr(s, '/') && !strchr(s, '\\');
}
static void get_stats(char *out, size_t cap) {
    double cpu = 0.0; long mem = 0, uptime = 0;
    FILE *f = fopen("/proc/loadavg", "r");
    if (f) { fscanf(f, "%lf", &cpu); fclose(f); cpu *= 100.0; if (cpu > 100.0) cpu = 100.0; }
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char key[64]; long val; char unit[32];
        while (fscanf(f, "%63s %ld %31s", key, &val, unit) == 3) {
            if (!strcmp(key, "MemAvailable:")) { mem = val / 1024; break; }
        }
        fclose(f);
    }
    f = fopen("/proc/uptime", "r");
    if (f) { double u = 0; fscanf(f, "%lf", &u); uptime = (long)u; fclose(f); }
    snprintf(out, cap, "SYSINFO %.1f %ld %ld", cpu, mem, uptime);
}
struct client_ctx { int fd; struct sockaddr_in peer; };
struct monitor_ctx { int active; int udp_port; int tcp_fd; struct sockaddr_in peer; };
static void *monitor_worker(void *arg) {
    struct monitor_ctx *m = arg;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return NULL;
    struct sockaddr_in dst = m->peer; dst.sin_port = htons((unsigned short)m->udp_port);
    while (m->active) {
        char stats[256]; get_stats(stats, sizeof(stats));
        char msg[320]; snprintf(msg, sizeof(msg), "%s SID:%s", stats, SID);
        sendto(s, msg, strlen(msg), 0, (struct sockaddr *)&dst, sizeof(dst));
        for (int i=0; i<10 && m->active; i++) usleep(100000);
    }
    close(s); return NULL;
}
static void *client_worker(void *arg) {
    struct client_ctx *ctx = arg;
    int fd = ctx->fd;
    struct sockaddr_in peer = ctx->peer;
    free(ctx);
    char line[MAX_LINE];
    int authenticated = 0;
    struct monitor_ctx mon; memset(&mon, 0, sizeof(mon));
    pthread_t mon_tid; int mon_started = 0;
    char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
    log_event("CONNECT %s:%d", ip, ntohs(peer.sin_port));
    while (1) {
        ssize_t n = read_line(fd, line, sizeof(line));
        if (n <= 0) break;
        log_event("COMMAND from %s: %s", ip, line);
        if (!authenticated) {
            char supplied[256] = "";
            if (sscanf(line, "AUTH %255s", supplied) == 1 && strcmp(supplied, TOKEN) == 0) {
                authenticated = 1; respond(fd, "OK AUTHENTICATED"); 
            } else { respond(fd, "ERR 001 AUTH_FAILED"); }
            continue;
        }
        if (!strcmp(line, "SYSINFO")) {
            char stats[256]; get_stats(stats, sizeof(stats)); respond(fd, "OK %s", stats);
        } else if (!strcmp(line, "LISTPROC")) {
            FILE *p = popen("ps -eo pid=,comm= --no-headers 2>/dev/null | head -c 3000", "r");
            char buf[3200] = ""; if (p) { fread(buf, 1, sizeof(buf)-1, p); pclose(p); }
            for (char *q=buf; *q; q++) if (*q=='\n' || *q=='\r') *q=',';
            if (!*buf) strcpy(buf, "process-list-unavailable");
            respond(fd, "OK PROCS %s", buf);
        } else if (!strncmp(line, "EXEC ", 5)) {
            const char *name = line + 5; char cmd[128] = "";
            if (!strcmp(name,"DATE")) strcpy(cmd, "date");
            else if (!strcmp(name,"UPTIME")) strcpy(cmd, "uptime -p");
            else if (!strcmp(name,"DISKFREE")) strcpy(cmd, "df -h /");
            else if (!strcmp(name,"HOSTNAME")) strcpy(cmd, "hostname");
            else if (!strcmp(name,"WHOAMI")) strcpy(cmd, "whoami");
            else { respond(fd, "ERR 002 COMMAND_NOT_ALLOWED"); continue; }
            FILE *p = popen(cmd, "r"); char out[1800] = "";
            if (p) { fread(out, 1, sizeof(out)-1, p); pclose(p); }
            for (char *q=out; *q; q++) if (*q=='\n' || *q=='\r') *q=' ';
            if (!*out) strcpy(out, "(no output)");
            respond(fd, "OK EXEC_RESULT %s", out);
        } else if (!strncmp(line, "PUT ", 4)) {
            char filename[256]; unsigned long size;
            if (sscanf(line+4, "%255s %lu", filename, &size) != 2 || !safe_name(filename)) {
                respond(fd, "ERR 003 INVALID_PUT"); continue;
            }
            if (size > MAX_FILE) { respond(fd, "ERR 004 FILE_TOO_LARGE"); continue; }
            char dir[512]; snprintf(dir, sizeof(dir), "./agentfiles/IT24102925");
            char mkdircmd[600]; snprintf(mkdircmd, sizeof(mkdircmd), "mkdir -p '%s'", dir); (void)system(mkdircmd);
            char path[800]; snprintf(path, sizeof(path), "%s/%s", dir, filename);
            FILE *f = fopen(path, "wb");
            if (!f) { respond(fd, "ERR 006 FILE_WRITE_FAILED"); continue; }
            char chunk[8192]; unsigned long left=size; int failed=0;
            while (left) {
                size_t want = left < sizeof(chunk) ? (size_t)left : sizeof(chunk);
                ssize_t r = recv(fd, chunk, want, 0);
                if (r <= 0) { failed=1; break; }
                if (fwrite(chunk, 1, (size_t)r, f) != (size_t)r) { failed=1; break; }
                left -= (unsigned long)r;
            }
            fclose(f);
            if (failed) { unlink(path); break; }
            log_event("PUT %s bytes=%lu from %s", filename, size, ip);
            respond(fd, "OK FILE_RECEIVED %s", filename);
        } else if (!strncmp(line, "GET ", 4)) {
            char filename[256];
            if (sscanf(line+4, "%255s", filename) != 1 || !safe_name(filename)) { respond(fd, "ERR 005 FILE_NOT_FOUND"); continue; }
            char path[800]; snprintf(path, sizeof(path), "./agentfiles/IT24102925/%s", filename);
            FILE *f = fopen(path, "rb");
            if (!f) { respond(fd, "ERR 005 FILE_NOT_FOUND"); continue; }
            fseek(f, 0, SEEK_END); long sz=ftell(f); rewind(f);
            if (sz < 0 || (unsigned long)sz > MAX_FILE) { fclose(f); respond(fd, "ERR 006 FILE_READ_FAILED"); continue; }
            respond(fd, "OK FILE_SEND %s %ld", filename, sz);
            char chunk[8192]; size_t r;
            while ((r=fread(chunk,1,sizeof(chunk),f))>0) if (send_all(fd,chunk,r)<0) break;
            fclose(f); log_event("GET %s bytes=%ld to %s", filename, sz, ip);
        } else if (!strncmp(line, "MONITOR START ", 14)) {
            int port=atoi(line+14);
            if (port < 1 || port > 65535) { respond(fd, "ERR 007 INVALID_UDP_PORT"); continue; }
            if (mon_started) { mon.active=0; pthread_join(mon_tid,NULL); mon_started=0; }
            mon.active=1; mon.udp_port=port; mon.tcp_fd=fd; mon.peer=peer;
            if (pthread_create(&mon_tid,NULL,monitor_worker,&mon)==0) { mon_started=1; respond(fd,"OK MONITOR_STARTED"); }
            else { mon.active=0; respond(fd,"ERR 008 MONITOR_FAILED"); }
        } else if (!strcmp(line, "MONITOR STOP")) {
            if (mon_started) { mon.active=0; pthread_join(mon_tid,NULL); mon_started=0; }
            respond(fd, "OK MONITOR_STOPPED");
        } else if (!strcmp(line, "QUIT")) {
            if (mon_started) { mon.active=0; pthread_join(mon_tid,NULL); }
            respond(fd, "OK BYE"); break;
        } else {
            respond(fd, "ERR 009 UNKNOWN_COMMAND");
        }
    }
    if (mon_started) { mon.active=0; pthread_join(mon_tid,NULL); }
    log_event("DISCONNECT %s:%d", ip, ntohs(peer.sin_port));
    close(fd); return NULL;
}
int main(void) {
    signal(SIGPIPE, SIG_IGN);
    int s=socket(AF_INET,SOCK_STREAM,0);
    if(s<0){perror("socket");return 1;}
    int yes=1; setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    struct sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(PORT);
    if(bind(s,(struct sockaddr*)&a,sizeof(a))<0){perror("bind");close(s);return 1;}
    if(listen(s,10)<0){perror("listen");close(s);return 1;}
    printf("RemoteOps Agent listening on TCP port %d (SID:%s)\n",PORT,SID);
    log_event("Agent started on port %d",PORT);
    while(1){
        struct sockaddr_in peer; socklen_t len=sizeof(peer);
        int c=accept(s,(struct sockaddr*)&peer,&len);
        if(c<0){if(errno==EINTR)continue;perror("accept");continue;}
        struct client_ctx *ctx=malloc(sizeof(*ctx)); if(!ctx){close(c);continue;}
        ctx->fd=c;ctx->peer=peer;
        pthread_t t;
        if(pthread_create(&t,NULL,client_worker,ctx)==0) pthread_detach(t);
        else {close(c);free(ctx);}
    }
    close(s);return 0;
}
