/**
 * @file BAsioCompat.hpp
 *
 * @brief Compatibility shim for the boost::asio::io_service to io_context rename.
 *
 * Boost 1.84 removed the long-deprecated boost::asio::io_service type alias,
 * its corresponding header <boost/asio/io_service.hpp>, and the old
 * io_service::work type. This header provides RobWork-local aliases so the
 * surrounding code can compile with both older and newer Boost.Asio versions.
 */
#if !defined(RW_COMMON_B_ASIO_COMPAT_HPP)
#define RW_COMMON_B_ASIO_COMPAT_HPP

#include <boost/version.hpp>

#if BOOST_VERSION >= 108400
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#else
#include <boost/asio/io_service.hpp>
#endif

namespace rw { namespace common { namespace basio {
#if BOOST_VERSION >= 108400
    typedef boost::asio::io_context Service;
    typedef boost::asio::executor_work_guard< Service::executor_type > Work;

    inline Work* makeWork (Service& service)
    {
        return new Work (boost::asio::make_work_guard (service));
    }

    template< class Handler > void post (Service& service, Handler handler)
    {
        boost::asio::post (service, handler);
    }
#else
    typedef boost::asio::io_service Service;
    typedef boost::asio::io_service::work Work;

    inline Work* makeWork (Service& service)
    {
        return new Work (service);
    }

    template< class Handler > void post (Service& service, Handler handler)
    {
        service.post (handler);
    }
#endif
}}}

#endif    // RW_COMMON_B_ASIO_COMPAT_HPP
