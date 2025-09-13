#if	!defined(__librustyaxe_ws_h)
#define	__librustyaxe_ws_h

struct ws_conn {
   bool			 ws_connected;
   struct mg_connection *ws_conn;
   
};
typedef struct ws_conn ws_conn_t;
#endif	// !defined(__librustyaxe_ws_h)
