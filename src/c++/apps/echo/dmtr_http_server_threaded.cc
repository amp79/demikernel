#include <boost/chrono.hpp>
#include <boost/optional.hpp>

#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#include <unordered_map>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <queue>

#include "common.hh"
#include "request_parser.h"
#include <dmtr/annot.h>
#include <dmtr/latency.h>
#include <dmtr/libos.h>
#include <dmtr/wait.h>
#include <dmtr/libos/mem.h>

int lqd = 0;
class Worker {
    public: int my_memqfd;
};
std::vector<Worker *> http_workers;
std::vector<pthread_t *> worker_threads;

dmtr_latency_t *pop_latency = NULL;
dmtr_latency_t *push_latency = NULL;
dmtr_latency_t *push_wait_latency = NULL;

void sig_handler(int signo) {
    /*
    dmtr_dump_latency(stderr, pop_latency);
    dmtr_dump_latency(stderr, push_latency);
    dmtr_dump_latency(stderr, push_wait_latency);
    */

    dmtr_close(lqd);
    http_workers.clear();
    for (pthread_t *w: worker_threads) {
        pthread_kill(*w, SIGKILL);
    }
    exit(0);
}

std::queue<int> accepted_qfds;
pthread_mutex_t qfds_mutex;

int match_filter(std::string message) {
    if (message.find("\r\n") != std::string::npos) {
        std::string request_line = message.substr(0, message.find("\r\n"));
        size_t next = 0;
        size_t last = 0;
        int i = 1;
        while ((next = request_line.find(' ', last)) != std::string::npos) {
            if (i == 3) {
                std::string http = request_line.substr(last, next-last);
                if (http.find("HTTP") != std::string::npos) {
                    //that looks like an HTTP request line
                    printf("Got an HTTP request: %s\n", request_line.c_str());
                    return 1;
                }
                return 0;
            }
            last = next + 1;
            ++i;
        }
    }
    return 0;
}

static void *http_work(void *args) {
    printf("Hello I am an HTTP worker\n");
    struct parser_state *state =
        (struct parser_state *) malloc(sizeof(*state));

    std::string answer_ok("200 OK");
    std::string answer_nok("400 BAD REQUEST");

    Worker *me = (Worker *) args;
    dmtr_qtoken_t token = 0;
    dmtr_qresult_t wait_out;
    while (1) {
        dmtr_pop(&token, me->my_memqfd);
        int status = dmtr_wait(&wait_out, token);
        if (status == 0) {
            assert(DMTR_OPC_POP == wait_out.qr_opcode);
            assert(wait_out.qr_value.sga.sga_numsegs == 1);
            fprintf(stdout, "HTTP worker received %s\n",
                     reinterpret_cast<char *>(wait_out.qr_value.sga.sga_segs[0].sgaseg_buf));

            init_parser_state(state);
            size_t msg_size = (size_t) wait_out.qr_value.sga.sga_segs[0].sgaseg_len;
            char *msg = (char *) wait_out.qr_value.sga.sga_segs[0].sgaseg_buf;
            enum parser_status status = parse_http(state, msg, msg_size);
            switch (status) {
                case REQ_COMPLETE:
                    fprintf(stdout, "HTTP worker got complete request\n");
                    wait_out.qr_value.sga.sga_segs[0].sgaseg_len = answer_ok.size();
                    strncpy((char *) wait_out.qr_value.sga.sga_segs[0].sgaseg_buf,
                             answer_ok.c_str(), answer_ok.size());
                    break;
                case REQ_ERROR:
                    fprintf(stdout, "HTTP worker got malformed request\n");
                    wait_out.qr_value.sga.sga_segs[0].sgaseg_len = answer_nok.size();
                    strncpy((char *) wait_out.qr_value.sga.sga_segs[0].sgaseg_buf,
                             answer_nok.c_str(), answer_nok.size());
                    break;
                case REQ_INCOMPLETE:
                    fprintf(stdout, "HTTP worker got incomplete request: %.*s\n",
                        (int) msg_size, msg);
                    wait_out.qr_value.sga.sga_segs[0].sgaseg_len = answer_nok.size();
                    strncpy((char *) wait_out.qr_value.sga.sga_segs[0].sgaseg_buf,
                             answer_nok.c_str(), answer_nok.size());
                    break;
            }
            //free(wait_out.qr_value.sga.sga_segs[0].sgaseg_buf);

            dmtr_push(&token, wait_out.qr_qd, &wait_out.qr_value.sga);
            dmtr_wait(NULL, token);
            free(state->url);
            free(state->body);
        }
    }

    free(state);
    return NULL;
}

static void *tcp_work(void *args) {
    printf("Hello I am a TCP worker\n");
    int num_rcvd = 0;
    std::vector<dmtr_qtoken_t> tokens;
    dmtr_qtoken_t token = 0; //temporary token
    while (1) {
        pthread_mutex_lock(&qfds_mutex);
        while(!accepted_qfds.empty()) {
            int qfd = accepted_qfds.front();
            accepted_qfds.pop();
            dmtr_pop(&token, qfd); //enable the queue for reading
            tokens.push_back(token);
        }
        pthread_mutex_unlock(&qfds_mutex);

        if (tokens.empty()) {
            continue;
        }

        dmtr_qresult_t wait_out;
        int idx;
        int status = dmtr_wait_any(&wait_out, &idx, tokens.data(), tokens.size());
        if (status == 0) {
            assert(DMTR_OPC_POP == wait_out.qr_opcode);
            assert(wait_out.qr_value.sga.sga_numsegs == 1);
            /*
            fprintf(stdout, "received %s on queue %d\n",
                     reinterpret_cast<char *>(wait_out.qr_value.sga.sga_segs[0].sgaseg_buf),
                     wait_out.qr_qd);
            */
            num_rcvd++;
            printf("received: %d requests\n", num_rcvd);

            token = tokens[idx];
            tokens.erase(tokens.begin()+idx);

            int worker_idx = (num_rcvd % http_workers.size());
            fprintf(stdout, "passing to http worker #%d\n", worker_idx);
            dmtr_push(&token, http_workers[worker_idx]->my_memqfd, &wait_out.qr_value.sga);
            dmtr_wait(NULL, token);
            // Wait for HTTP worker to give us an answer
            dmtr_pop(&token, http_workers[worker_idx]->my_memqfd);
            dmtr_qresult_t http_out;
            dmtr_wait(&http_out, token);
            // Answer the client
            dmtr_push(&token, wait_out.qr_qd, &http_out.qr_value.sga);
            dmtr_wait(NULL, token);
            // Re-enable TCP queue for reading
            dmtr_pop(&token, wait_out.qr_qd);
            free(wait_out.qr_value.sga.sga_buf); // is it the same reference shared by http_out?

            tokens.push_back(token);
        } else {
            assert(status == ECONNRESET || status == ECONNABORTED);
            dmtr_close(wait_out.qr_qd);
            tokens.erase(tokens.begin()+idx);
        }
    }
}

void pin_thread(pthread_t thread, uint16_t cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    int rtn = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (rtn != 0) {
        fprintf(stderr, "could not pin thread: %s\n", strerror(errno));
    }
}

int main(int argc, char *argv[]) {
    uint16_t n_http_workers;
    options_description desc{"HTTP server options"};
    desc.add_options()
        ("http-workers,w", value<uint16_t>(&n_http_workers)->default_value(1), "num HTTP workers");
    parse_args(argc, argv, true, desc);

    struct sockaddr_in saddr = {};
    saddr.sin_family = AF_INET;
    if (boost::none == server_ip_addr) {
        std::cerr << "Listening on `*:" << port << "`..." << std::endl;
        saddr.sin_addr.s_addr = INADDR_ANY;
    } else {
        const char *s = boost::get(server_ip_addr).c_str();
        std::cerr << "Listening on `" << s << ":" << port << "`..." << std::endl;
        if (inet_pton(AF_INET, s, &saddr.sin_addr) != 1) {
            std::cerr << "Unable to parse IP address." << std::endl;
            return -1;
        }
    }
    saddr.sin_port = htons(port);

    /* Block SIGINT to ensure handler will only be run in main thread */
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    int ret;
    ret = pthread_sigmask(SIG_BLOCK, &mask, &oldmask);
    if (ret != 0) {
        fprintf(stderr, "Couln't block SIGINT: %s\n", strerror(errno));
    }

    /* Init Demeter */
    DMTR_OK(dmtr_init(dmtr_argc, dmtr_argv));

    /* Pin main thread */
    pin_thread(pthread_self(), 0);

    /* Create http worker threads */
    for (int i = 0; i < n_http_workers; ++i) {
        Worker *worker = new Worker();
        worker->my_memqfd = -1;
        dmtr_queue(&worker->my_memqfd);
        http_workers.push_back(worker);

        pthread_t http_worker;
        worker_threads.push_back(&http_worker);
        pthread_create(&http_worker, NULL, &http_work, (void *) worker);
        pin_thread(http_worker, i+1);
    }

    /* Create TCP worker thread */
    pthread_mutex_init(&qfds_mutex, NULL);
    pthread_t tcp_worker;
    worker_threads.push_back(&tcp_worker);
    pthread_create(&tcp_worker, NULL, &tcp_work, NULL);
    pin_thread(tcp_worker, n_http_workers+1);

    /* Re-enable SIGINT and SIGQUIT */
    ret = pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
    if (ret != 0) {
        fprintf(stderr, "Couln't block SIGINT: %s\n", strerror(errno));
    }

    dmtr_qtoken_t token;
    DMTR_OK(dmtr_socket(&lqd, AF_INET, SOCK_STREAM, 0));
    DMTR_OK(dmtr_bind(lqd, reinterpret_cast<struct sockaddr *>(&saddr), sizeof(saddr)));
    DMTR_OK(dmtr_listen(lqd, 3));

    if (signal(SIGINT, sig_handler) == SIG_ERR)
        std::cout << "\ncan't catch SIGINT\n";

    while (1) {
        DMTR_OK(dmtr_accept(&token, lqd));
        dmtr_qresult wait_out;
        int status = dmtr_wait(&wait_out, token);
        // if we got an EOK back from wait
        if (status == 0) {
            pthread_mutex_lock(&qfds_mutex);
            accepted_qfds.push(wait_out.qr_value.ares.qd);
            pthread_mutex_unlock(&qfds_mutex);
        } else {
            fprintf(stdout, "dmtr_wait on accept socket got status %d\n",
                    status);
        }
    }

    return 0;
}
