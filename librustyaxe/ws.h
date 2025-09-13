struct ws_conn {
   bool			 ws_connected;
   struct mg_connection *ws_conn;
   
};
typedef struct ws_conn ws_conn_t;
