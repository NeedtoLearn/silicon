#pragma once

#include <microhttpd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <silicon/error.hh>
#include <silicon/service.hh>
#include <silicon/tracking_cookie.hh>
#include <iod/json.hh>

namespace sl
{

  struct mhd_request
  {
    mhd_request(MHD_Connection* con, const std::string b) : connection(con), body(b) {}

    MHD_Connection* connection;
    std::string body;
  };

  struct mhd_response
  {
    int status;
    std::string body;
    std::vector<std::pair<std::string, std::string>> cookies;
  };
    
  struct mhd_json_service_utils
  {
    typedef mhd_request request_type;
    typedef mhd_response response_type;

    template <typename T>
    auto deserialize(const request_type& r, T& res) const
    {
      try
      {
        json_decode(res, r.body);
      }
      catch (const std::runtime_error& e)
      {
        throw error::bad_request("Error when decoding procedure arguments: ", e.what());
      }

    }

    template <typename T>
    auto serialize(response_type& r, const T& res) const
    {
      std::string str = json_encode(res);
      r.status = 200;
      r.body = str;
    }

  };

  template <typename F>
  int mdh_keyvalue_iterator(void *cls,
                            enum MHD_ValueKind kind,
                            const char *key, const char *value)
  {
    F& f = *(F*)cls;
    f(key, value);
  }

  struct mhd_session_cookie
  {
    
    inline tracking_cookie instantiate(mhd_request* req, mhd_response* resp)
    {
      std::string key = "sl_token";
      std::string token;
      const char* token_ = MHD_lookup_connection_value(req->connection, MHD_COOKIE_KIND, key.c_str());
      
      if (!token_)
      {
        token = generate_secret_tracking_id();
        resp->cookies.push_back(std::make_pair(key, token));
      }
      else
        token = token_;

      return tracking_cookie(token);
    }

  };
  
  template <typename S>
  int mhd_handler(void * cls,
                  struct MHD_Connection * connection,
                  const char * url,
                  const char * method,
                  const char * version,
                  const char * upload_data,
                  size_t * upload_data_size,
                  void ** ptr)
  {
    static int dummy;
    MHD_Response* response;
    int ret;

    std::string* pp = (std::string*)*ptr;
    if (!pp)
    {
      pp = new std::string;
      *ptr = pp;
      return MHD_YES;
    }
    if (*upload_data_size)
    {
      pp->append(std::string(upload_data, *upload_data_size));
      *upload_data_size = 0;
      return MHD_YES;
    }

    auto& service = * (S*)cls;

    mhd_request rq(connection, *pp);
    mhd_response resp;

    try
    {
      service(url, rq, resp);
    }
    catch(const error::error& e)
    {
      resp.status = e.status();
      std::string m = e.what();
      resp.body = m.data();
    }
    catch(const std::runtime_error& e)
    {
      std::cout << e.what() << std::endl;
      resp.status = 500;
      resp.body = "Internal server error.";
    }

    const std::string& str = resp.body;
    response = MHD_create_response_from_data(str.size(),
                                             (void*) str.c_str(),
                                             MHD_NO,
                                             MHD_YES);
    // Set cookies.
    for(auto kv : resp.cookies)
    {
      std::string set_cookie_string = kv.first + '=' + kv.second;
      if (MHD_NO == MHD_add_response_header (response,
                                             MHD_HTTP_HEADER_SET_COOKIE,
                                             set_cookie_string.c_str()))
        throw error::internal_server_error("Failed to set session cookie header");
    }
    
    ret = MHD_queue_response(connection,
                             resp.status,
                             response);
    MHD_destroy_response(response);
    delete pp;
    return ret;
  }

  template <typename A>
  void mhd_json_serve(const A& api, int port)
  {

    auto service = make_service<mhd_json_service_utils>(api.bind_middlewares(mhd_session_cookie()));
    typedef decltype(service) S;

    struct MHD_Daemon * d;
    d = MHD_start_daemon(
      //MHD_USE_THREAD_PER_CONNECTION,
      //MHD_USE_EPOLL_INTERNALLY_LINUX_ONLY | MHD_USE_EPOLL_TURBO | MHD_USE_TCP_FASTOPEN,
      MHD_USE_SELECT_INTERNALLY,
      port,
      NULL,
      NULL,
      &mhd_handler<S>,
      &service,
      MHD_OPTION_THREAD_POOL_SIZE, 3,
      MHD_OPTION_END);
    if (d == NULL)
      return;

    while (getc (stdin) != 'q');
    MHD_stop_daemon(d);
  }
  

}
