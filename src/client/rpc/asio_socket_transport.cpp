#include "asio_socket_transport.h"
#include "mir/variable_length_array.h"

#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <system_error>

#include <boost/exception/errinfo_errno.hpp>
#include <boost/throw_exception.hpp>



namespace mclr = mir::client::rpc;

mclr::AsioSocketTransport::AsioSocketTransport(int fd)
    : work{io_service},
      socket{io_service}
{
    socket.assign(boost::asio::local::stream_protocol(), fd);
    init();
}

mclr::AsioSocketTransport::AsioSocketTransport(std::string const& socket_path)
    : work{io_service},
      socket{io_service}
{
    socket.connect(socket_path);
    init();
}

mclr::AsioSocketTransport::~AsioSocketTransport()
{
    io_service.stop();

    if (io_service_thread.joinable())
    {
        io_service_thread.join();
    }
}

void mclr::AsioSocketTransport::register_observer(std::shared_ptr<Observer> const& observer)
{
    std::lock_guard<decltype(observer_mutex)> lock(observer_mutex);
    observers.push_back(observer);
}

void mclr::AsioSocketTransport::receive_data(void* buffer, size_t read_bytes)
{
    ssize_t bytes_read = recv(socket.native_handle(), buffer, read_bytes, MSG_NOSIGNAL);

    if (bytes_read < 0)
    {
        if (errno == EPIPE)
        {
            notify_disconnected();
        }
        BOOST_THROW_EXCEPTION(
                    boost::enable_error_info(
                        std::runtime_error(std::string("Failed to read message from server:")
                                           + strerror(errno))) << boost::errinfo_errno(errno));
    }
}

void mclr::AsioSocketTransport::receive_data(void* buffer, size_t read_bytes, std::vector<int> &fds)
{
    // Store the data in the buffer requested
    struct iovec iov;
    iov.iov_base = buffer;
    iov.iov_len = read_bytes;

    // Allocate space for control message
    static auto const builtin_n_fds = 5;
    static auto const builtin_cmsg_space = CMSG_SPACE(builtin_n_fds * sizeof(int));
    auto const fds_bytes = fds.size() * sizeof(int);
    mir::VariableLengthArray<builtin_cmsg_space> control{CMSG_SPACE(fds_bytes)};

    // Message to read
    struct msghdr header;
    header.msg_name = NULL;
    header.msg_namelen = 0;
    header.msg_iov = &iov;
    header.msg_iovlen = 1;
    header.msg_controllen = control.size();
    header.msg_control = control.data();
    header.msg_flags = 0;

    ssize_t bytes_read = recvmsg(socket.native_handle(), &header, MSG_NOSIGNAL);

    if (bytes_read < 0)
    {
        if (errno == EPIPE)
        {
            notify_disconnected();
        }
        BOOST_THROW_EXCEPTION(
                    boost::enable_error_info(
                        std::runtime_error(std::string("Failed to read message from server:")
                                           + strerror(errno))) << boost::errinfo_errno(errno));
    }

    if (fds.size() > 0)
    {
        // If we get a proper control message, copy the received
        // file descriptors back to the caller
        struct cmsghdr const* const cmsg = CMSG_FIRSTHDR(&header);

        if (cmsg && cmsg->cmsg_len == CMSG_LEN(fds_bytes) &&
            cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
        {
            int const* const data = reinterpret_cast<int const*>CMSG_DATA(cmsg);
            int i = 0;
            for (auto& fd : fds)
                fd = data[i++];
        }
        else
        {
            BOOST_THROW_EXCEPTION(
                        std::runtime_error("Invalid control message for receiving file descriptors"));
        }
    }
}

void mclr::AsioSocketTransport::send_data(const std::vector<uint8_t>& buffer)
{
    ssize_t bytes_written = send(socket.native_handle(), buffer.data(), buffer.size(), MSG_NOSIGNAL);

    if (bytes_written < 0)
    {
        if (errno == EPIPE)
        {
            notify_disconnected();
        }
        BOOST_THROW_EXCEPTION(
                    boost::enable_error_info(
                        std::runtime_error(std::string("Failed to send message to server:")
                                           + strerror(errno))) << boost::errinfo_errno(errno));
    }
}

void mclr::AsioSocketTransport::init()
{
    io_service_thread = std::thread([this]
        {
            // Our IO threads must not receive any signals
            sigset_t all_signals;
            sigfillset(&all_signals);

            if (auto error = pthread_sigmask(SIG_BLOCK, &all_signals, NULL))
                BOOST_THROW_EXCEPTION(
                    boost::enable_error_info(
                        std::runtime_error("Failed to block signals on IO thread")) << boost::errinfo_errno(error));

            socket.native_non_blocking(false);
            socket.async_read_some(boost::asio::null_buffers(),
                                   std::bind(&mclr::AsioSocketTransport::on_data_available, this,
                                             std::placeholders::_1, std::placeholders::_2));

            int pipefds[2];
            if (pipe(pipefds) < 0)
            {
                BOOST_THROW_EXCEPTION(std::system_error(errno,
                                                        std::system_category(),
                                                        "Failed to create shutdown pipes"));
            };

            int const shutdown_read_fd = pipefds[0];
            int const shutdown_write_fd = pipefds[1];

            auto disconnect_watcher = std::thread([this, shutdown_read_fd]
            {
                pollfd fds[2];
                // Watch for disconnect events on the native handle
                fds[0].fd = socket.native_handle();
                fds[0].events = POLLRDHUP; // poll automatically listens for the other error conditions
                // And also handle the shutdown dance
                fds[1].fd = shutdown_read_fd;
                fds[1].events = POLLIN;

                if (poll(fds, sizeof(fds) / sizeof(struct pollfd), -1) < 0)
                {
                    BOOST_THROW_EXCEPTION(std::system_error(errno,
                                                            std::system_category(),
                                                            "Failed to monitor for socket disconnect"));
                }

                if (fds[0].events & (POLLRDHUP | POLLERR | POLLHUP | POLLNVAL))
                {
                    notify_disconnected();
                }
            });
            try
            {
                io_service.run();
            }
            catch(...)
            {
                notify_disconnected();
            }

            if (disconnect_watcher.joinable())
            {
                int dummy{0};
                if (write(shutdown_write_fd, &dummy, sizeof(dummy)) < 0)
                {
                    BOOST_THROW_EXCEPTION(std::system_error(errno,
                                                            std::system_category(),
                                                            "Failed to send shutdown message"));
                }
                disconnect_watcher.join();
            }
            ::close(shutdown_read_fd);
            ::close(shutdown_write_fd);
    });
}

void mclr::AsioSocketTransport::on_data_available(boost::system::error_code const& /*ec*/, size_t /*bytes_read*/)
{
    notify_data_available();
    socket.async_read_some(boost::asio::null_buffers(),
                           std::bind(&mclr::AsioSocketTransport::on_data_available, this,
                                     std::placeholders::_1, std::placeholders::_2));

}

void mclr::AsioSocketTransport::notify_data_available()
{
    // TODO: If copying the observers turns out to be slow, replace with
    // an RCUish data type; this is a read-mostly, write infrequently structure.
    decltype(observers) observer_copy;
    {
        std::lock_guard<decltype(observer_mutex)> lock(observer_mutex);
        observer_copy = observers;
    }
    for (auto& observer : observer_copy)
    {
        observer->on_data_available();
    }
}

void mclr::AsioSocketTransport::notify_disconnected()
{
    decltype(observers) observer_copy;
    {
        std::lock_guard<decltype(observer_mutex)> lock(observer_mutex);
        observer_copy = observers;
    }
    for (auto& observer : observer_copy)
    {
        observer->on_disconnected();
    }
}
