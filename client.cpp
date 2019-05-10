//
// Created by emp on 2019-05-09.
//

#include <iostream>
#include <algorithm>
#include <thread>
#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/asio/steady_timer.hpp>

using namespace boost;
using boost::asio::ip::tcp;

class stats
{
public:
    stats(int timeout) : timeout_(timeout)
    {
    }

    void add(bool error, size_t count_written, size_t count_read,
             size_t bytes_written, size_t bytes_read)
    {
        total_error_count_ += error? 1: 0;
        total_count_written_ += count_written;
        total_count_read_ += count_read;
        total_bytes_written_ += bytes_written;
        total_bytes_read_ += bytes_read;
    }

    void print()
    {
        std::cout << total_error_count_ << " total count error\n";
        std::cout << total_count_written_ << " total count written\n";
        std::cout << total_count_read_ << " total count read\n";
        std::cout << total_bytes_written_ << " total bytes written\n";
        std::cout << total_bytes_read_ << " total bytes read\n";
        std::cout << static_cast<double>(total_bytes_read_) /
                     (timeout_ * 1024 * 1024) << " MiB/s read throughput\n";
        std::cout << static_cast<double>(total_bytes_written_) /
                     (timeout_ * 1024 * 1024) << " MiB/s write throughput\n";
    }

private:
    size_t total_error_count_ = 0;
    size_t total_bytes_written_ = 0;
    size_t total_bytes_read_ = 0;
    size_t total_count_written_ = 0;
    size_t total_count_read_ = 0;
    int timeout_;
};

class session : noncopyable
{
public:
    session(asio::io_service& ios, size_t block_size)
            : io_service_(ios), socket_(ios), block_size_(block_size),
              buffer_(new char[block_size]) {
        for (size_t i = 0; i < block_size_; ++i)
            buffer_[i] = static_cast<char>(i % 128);
    }

    ~session() {
        delete[] buffer_;
    }

    void write() {
        asio::async_write(socket_, asio::buffer(buffer_, block_size_),
                          [this](const boost::system::error_code& err, size_t cb) {
                              if (!err) {
                                  assert(cb == block_size_);
                                  bytes_written_ += cb;
                                  ++count_written_;
                                  read();
                              } else {
                                  if (!want_close_) {
                                      //std::cout << "write failed: " << err.message() << "\n";
                                      error_ = true;
                                  }
                              }
                          });
    }

    void read() {
        asio::async_read(socket_, asio::buffer(buffer_, block_size_),
                         [this](const boost::system::error_code& err, size_t cb) {
                             if (!err) {
                                 assert(cb == block_size_);
                                 bytes_read_ += cb;
                                 ++count_read_;
                                 write();
                             } else {
                                 if (!want_close_) {
                                     //std::cout << "read failed: " << err.message() << "\n";
                                     error_ = true;
                                 }
                             }
                         });
    }

    void start(asio::ip::tcp::endpoint endpoint) {
        socket_.async_connect(endpoint, [this](const boost::system::error_code& err) {
            if (!err)
            {
                asio::ip::tcp::no_delay no_delay(true);
                socket_.set_option(no_delay);
                write();
            }
        });
    }

    void stop() {
        io_service_.post([this]() {
            want_close_ = true;
            socket_.close();
        });
    }

    size_t bytes_written() const {
        return bytes_written_;
    }

    size_t bytes_read() const {
        return bytes_read_;
    }

    size_t count_written() const {
        return count_written_;
    }

    size_t count_read() const {
        return count_read_;
    }

    bool error() const {
        return error_;
    }
private:
    asio::io_service& io_service_;
    asio::ip::tcp::socket socket_;
    size_t const block_size_;
    char* const buffer_;
    size_t bytes_written_ = 0;
    size_t bytes_read_ = 0;
    size_t count_written_ = 0;
    size_t count_read_ = 0;
    bool error_ = false;
    bool want_close_ = false;
};


class client : boost::noncopyable {
public:

    client(char const* host, char const* port, int thread_count,
            size_t session_count, size_t block_size, int timeout)
    : thread_count_(thread_count), session_count_(session_count),
    timeout_seconds_(timeout),  stats_(timeout){
        io_services_.resize(thread_count_);
        io_works_.resize(thread_count_);
        threads_.resize(thread_count_);

        for (auto i=0; i<thread_count_; ++i) {
            io_services_[i].reset(new asio::io_service);
            io_works_[i].reset(new asio::io_service::work(*io_services_[i]));
            threads_[i].reset(new std::thread([this, i](){
                auto & io_service = *io_services_[i];
                try{
                    io_service.run();
                }catch (std::exception& e){
                    std::cerr<<"Catch exception: "<< e.what() << std::endl;
                }
            }));
        }

        stop_timer_.reset(new asio::steady_timer(*io_services_[0]));
        resolver_.reset(new asio::ip::tcp::resolver(*io_services_[0]));
        asio::ip::tcp::resolver::iterator iter = resolver_->resolve(asio::ip::tcp::resolver::query(host, port));
        endpoint_ = *iter;

        for(auto i=0; i<session_count_; i++) {
            auto& io_service = *io_services_[i % thread_count_];
            std::unique_ptr<session> new_session(new session(io_service, block_size));
            sessions_.emplace_back(std::move(new_session));
        }

    }


    ~client() {
        for (auto& session : sessions_) {
            stats_.add(
                    session->error(),
                    session->count_written(), session->count_read(),
                    session->bytes_written(), session->bytes_read());
        }

        stats_.print();
    }

    void start() {
        stop_timer_->expires_from_now(std::chrono::seconds(timeout_seconds_));
        stop_timer_->async_wait([this](const boost::system::error_code& error) {
            for (auto& io_work : io_works_) {
                io_work.reset();
            }

            for (auto& session : sessions_) {
                session->stop();
            }
        });

        for (auto& session : sessions_) {
            session->start(endpoint_);
        }
    }

    void wait() {
        for (auto& thread : threads_) {
            thread->join();
        }
    }


private:
    int const thread_count_;
    int const session_count_;
    int const timeout_seconds_;
    stats    stats_;
    asio::ip::tcp::endpoint endpoint_;
    std::unique_ptr<asio::ip::tcp::resolver> resolver_;
    std::unique_ptr<asio::steady_timer> stop_timer_;
    std::vector<std::unique_ptr<std::thread>>   threads_;
    std::vector<std::unique_ptr<session>>       sessions_;
    std::vector<std::unique_ptr<asio::io_service::work>> io_works_;
    std::vector<std::unique_ptr<asio::io_service>> io_services_;
};


int main(int ac, char *av[])
{

    try
    {
        if (ac != 7)
        {
            std::cerr << "Usage: server <ip> <port> <thread count> <session count> <block size>  <timeout seconds>\n";
            return 1;
        }

        client cli(av[1], av[2], std::atoi(av[3]), std::atoi(av[4]), std::atoi(av[5]), std::atoi(av[6]));
        cli.start();
        cli.wait();
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}