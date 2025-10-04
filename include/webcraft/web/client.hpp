#pragma once

#include "core.hpp"

namespace webcraft::web::client
{

    class web_client;
    class web_connection_builder;

    class web_client
    {
    public:
        web_client();
        virtual ~web_client();

        async_t(void) close();
        web_connection_builder connect();
    };

    class web_connection_builder
    {
    public:
        friend class web_client;

        web_connection_builder(web_client &client);
        virtual ~web_connection_builder();

    }
}