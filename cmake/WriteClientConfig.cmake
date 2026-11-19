if(NOT EXISTS "${OUT}")
    file(WRITE "${OUT}" "server_ip=127.0.0.1\nserver_port=7777\n")
endif()
