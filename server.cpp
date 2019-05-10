#include <algorithm>
#include <iostream>
#include <list>
#include <string>
#include <mutex>
#include <assert.h>
#include <vector>
#include <memory>
#include <queue>
#include <stack>
#include <thread>
#include "io_pool.h"

//#define USE_HANDLE_ALLOC
#ifdef USE_HANDLE_ALLOC
#include "handle_allocation.h"
#endif

namespace {

    class session : public std::enable_shared_from_this<session>, noncopyable {
    public:
        session(asio::io_service &ios, size_t block_size)
                : io_service_(ios), socket_(ios), block_size_(block_size), buffer_(new char[block_size]) {
        }

        ~session() {
            delete[] buffer_;
        }

        asio::ip::tcp::socket &socket() {
            return socket_;
        }

        void start() {
            asio::ip::tcp::no_delay no_delay(true);
            socket_.set_option(no_delay);
            read();
        }

        void write() {
            asio::async_write(socket_, asio::buffer(buffer_, block_size_),
#ifdef USE_HANDLE_ALLOC
            make_custom_alloc_handler(handler_memory_, [this, self = shared_from_this()](
                    const boost::system::error_code &err, size_t cb) {
                if (!err) {
                    assert(cb == block_size_);
                    read();
                }
            })
#else
                              [this, self = shared_from_this()](const boost::system::error_code &err, size_t cb) {
                                  if (!err) {
                                      assert(cb == block_size_);
                                      read();
                                  }
                              }
#endif
            );
        }

        void read() {
            asio::async_read(socket_, asio::buffer(buffer_, block_size_),
#ifdef USE_HANDLE_ALLOC
            make_custom_alloc_handler(handler_memory_, [this, self = shared_from_this()](
                    const boost::system::error_code &err, size_t cb) {
                if (!err) {
                    assert(cb == block_size_);
                    write();
                }
            })
#else
                             [this, self = shared_from_this()](
                                     const boost::system::error_code &err, size_t cb) {
                                 if (!err) {
                                     assert(cb == block_size_);
                                     write();
                                 }
                             }
#endif
            );
        }

    private:
#ifdef USER_HANDLE_ALLOC
        handler_memory handler_memory_;
#endif
        asio::io_service &io_service_;
        asio::ip::tcp::socket socket_;
        size_t const block_size_;
        char *buffer_;
    };

    class server : noncopyable {
    public:
        server(int thread_count, const asio::ip::tcp::endpoint &endpoint,
               size_t block_size)
                : thread_count_(thread_count), block_size_(block_size), service_pool_(thread_count),
                  acceptor_(service_pool_.get_io_service()) {
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(1));
            acceptor_.bind(endpoint);
            acceptor_.listen();
        }

        void start() {
            accept();
        }

        void wait() {
            service_pool_.run();
        }

    private:
        void accept() {
            std::shared_ptr<session> new_session(new session(
                    service_pool_.get_io_service(), block_size_));
            auto &socket = new_session->socket();
            acceptor_.async_accept(socket,
                                   [this, new_session = std::move(new_session)](
                                           const boost::system::error_code &err) {
                                       if (!err) {
                                           new_session->start();
                                           accept();
                                       }
                                   });
        }

    private:
        int const thread_count_;
        size_t const block_size_;
        io_service_pool service_pool_;
        asio::ip::tcp::acceptor acceptor_;
    };

} // namespace

void usage(char *av[]) {
    std::cout << av[0] << " <host> <port> <thread count> <block size>" << std::endl;
}

int main(int ac, char *av[]) {

    if (ac != 5) {
        usage(av);
        return -1;
    }

    try {
        auto endpoint = asio::ip::tcp::endpoint(
                asio::ip::address::from_string(av[1]), atoi(av[2]));

        server s(std::atoi(av[3]), endpoint, std::atoi(av[4]));
        s.start();
        s.wait();
    } catch (std::exception &e) {
        std::cerr << "Got execption:" << e.what() << std::endl;
    }
    return 0;
}