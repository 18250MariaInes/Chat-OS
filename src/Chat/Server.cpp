#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 
#include <map>
#include <queue> 
#include <iostream>
#include <stdarg.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include "new.pb.h"
#include <stdio.h>
#include <ctime>
#include <bits/stdc++.h>
#include <sys/time.h>

using namespace std;
using namespace chat;

#ifndef MESSAGE_SIZE
#define MESSAGE_SIZE 8192
#endif

#ifndef MAX_QUEUE
#define MAX_QUEUE 20
#endif

#ifndef gettid
#define gettid() syscall(SYS_gettid)
#endif

#ifndef server_message_options
enum server_message_options {
    USERREGISTRATION = 1,
    CONNECTEDUSERRESPONSE = 2,
    CHANGESTATUSRESPONSE = 3,
    MESSAGECOMMUNICATION = 4,
    USERINFORESPONSE = 5,
};
#endif

#ifndef server_message_code
enum server_message_code {
    SUCCESS = 200,
    FAIL = 500,
};
#endif

#ifndef connected_user
struct connected_user {
    string name;
    string status;
    string ip;
};
#endif

#ifndef client_message_options
enum client_message_options {
    SYNCHRONIZED = 1,
    CONNECTEDUSER = 2,
    CHANGESTATUS = 3,
    DMMESSAGE = 4,
    ACKNOWLEDGE = 5
};
#endif

#ifndef message_received
struct message_received {
    string from_username;
    string message;
    string to_username;
};
#endif


#ifndef client_info
struct client_info {
    struct sockaddr_in socket_info;
    int socket_fd;
    int req_fd;
    string name;
    string ip;
    string status;
    // unsigned t0;
    //time_t t0;
    time_t start;
};
#endif

class Server {
    public:
        struct sockaddr_in _serv_addr, _cl_addr;
        int _sock;
        int _user_count;
        int _port;
        
        Server( int port, FILE *log_level = stdout ) {
	    _user_count = 1;
	    _port = port;
	    _log_level = log_level;
	    pthread_mutex_init( &_user_list_mutex, NULL );
	    pthread_mutex_init( &_req_queue_mutex, NULL );
	}

        
        int initiate() {
	    /* Create socket for server */
	    fprintf(_log_level, "Creando socket en el servidor\n");
	    if( ( _sock = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 ) {
		fprintf(_log_level, "Error en la creacion del socket\n");
		return -1;
	    }

	    fprintf(_log_level, "Creado correctamente con: %d\n", _sock);

	    /* Bind server to port */
	    fprintf(_log_level, "Socket %d en el puerto %d\n", _sock, _port);
	    _serv_addr.sin_family = AF_INET; 
	    _serv_addr.sin_addr.s_addr = INADDR_ANY; 
	    _serv_addr.sin_port = htons( _port ); // Convert to host byte order

	    if( bind(_sock, (struct sockaddr *)&_serv_addr, sizeof(_serv_addr)) < 0 ) { // Bind server address to socket
		fprintf(_log_level, "Error accediendo a la direccion del socket\n");
		return -1;
	    }

	    /* Set socket to listen for connections */
	    fprintf(_log_level, "Esperando por conecciones...\n");
	    if( listen( _sock, MAX_QUEUE ) < 0 ) { // queue MAX_QUEUE requests before refusing
		fprintf(_log_level, "Error en la escucha de mensajes\n");
		return -1;
	    }

	    return 0;
	}

        
        void start() {
	    /* Server infinite loop */
	    pthread_t thread;
	    fprintf(_log_level, "Esperando en el puerto %d...\n", _port);
	    while(1) {
		int client_id = listen_connections();
		//fprintf(_log_level, "DEBUG: Processing connection on new thread...\n");
		pthread_create( &thread, NULL, &new_conn_h, this );
	    }
	}

        
        void check_status() {
	    time_t end;
	    time(&end);
	
	    map<std::string, client_info> all_users = get_all_users();
	    map<std::string, client_info>::iterator it;
	    
	    for( it = all_users.begin(); it != all_users.end(); it++ ) {
		fprintf(_log_level, "Revisando tiempo de usuario %s\n", it->first.c_str());
	    	
		if (difftime(end, it->second.start) > 30 && it->second.status != "ocupado") {
			pthread_mutex_lock( &_user_list_mutex );
	    		_user_list[ it->second.name ].status = "inactivo";
	    		pthread_mutex_unlock( &_user_list_mutex );
		}
		
		if (difftime(end, it->second.start) < 30 && it->second.status != "activo") {
			pthread_mutex_lock( &_user_list_mutex );
	    		_user_list[ it->second.name ].status = "activo";
	    		pthread_mutex_unlock( &_user_list_mutex );
		}
	    }
        }
        
        int listen_connections() {
	    /* Accept incoming request */
	    int addr_size = sizeof(_cl_addr);
	    int req_fd = accept( _sock, (struct sockaddr *)&_cl_addr, (socklen_t*)&addr_size );
	    if(req_fd < 0) {
		fprintf(_log_level, "Error en la conexion con el cliente\n");
		return -1;
	    }

	    fprintf( _log_level, "Solicitud acceptada con: %d\n", req_fd );

	    /* Get the incomming request ip */
	    char ipstr[ INET6_ADDRSTRLEN ];
	    struct sockaddr_in *tmp_s = (struct sockaddr_in *)&_cl_addr;
	    inet_ntop( AF_INET, &tmp_s->sin_addr, ipstr, sizeof( ipstr ) );
	    fprintf(_log_level, "Solicitud entrante de la IP %s\n", ipstr);

	    /* Save request info to queue */
	    struct client_info new_cl;
	    new_cl.socket_info = _cl_addr;
	    new_cl.req_fd = req_fd;
	    new_cl.ip = ipstr;
	    time_t actual;
	    new_cl.start = time(&actual);
	    req_push( new_cl );
	    _user_count++;

	    fprintf(_log_level, "Solicitud agregada con id: %d\n", _user_count);

	    return 0;
	}

        
        int read_request( int fd, void *buf ) {
	    /* Read request info on socket */
	    fprintf(_log_level, "Esperando por solicitud de: %d \n", fd);
	    int read_size;
	    if( ( read_size = recvfrom( fd, buf, MESSAGE_SIZE, 0, NULL, NULL ) ) < 0 ) {
		fprintf(_log_level, "Error al leer la solicitud");
		return -1;
	    }
	    return read_size;
	}

        
        int send_response( int sock_fd, struct sockaddr_in *dest, ServerResponse res ) {
	    /* Serealizing response */
	    string dsrl_res;
	    res.SerializeToString(&dsrl_res);

	    // Converting to c string
	    char c_str[ dsrl_res.size() + 1 ];
	    strcpy( c_str, dsrl_res.c_str() );
	    /* Sending response */
	    fprintf(_log_level, "Enviando respuesta a %ld con %d...\n", sizeof( c_str ), sock_fd);
	    if( sendto( sock_fd, c_str, sizeof(c_str), 0, (struct sockaddr *) &dest, sizeof( &dest ) ) < 0 ) {
		fprintf(_log_level, "Error enviando la respuesta\n");
		return -1;
	    }
	    return 0;
	}

        
        
        ServerResponse process_request( ClientPetition cl_msg, client_info cl ) {
	    /* Get option */
	    int option = cl_msg.option();

	    /* Process acording to option */
	    fprintf(_log_level, "Procesando solicitud con opcion: %d\n", option);
	    switch (option) {
		case CONNECTEDUSER:
		    return get_connected_users();
		case CHANGESTATUS:
		    return change_user_status( cl_msg.change(), cl.name );
		case DMMESSAGE:
		    return broadcast_message( cl_msg.messagecommunication(), cl );
		case USERINFORESPONSE:
		    return get_user_info( cl_msg.users() );
	    }
	}

        
        
        ServerResponse broadcast_message( MessageCommunication req, client_info sender ) {
	    if (!req.has_recipient()){
	        return error_response(MESSAGECOMMUNICATION);
	    }
	    string recipient = req.recipient();
	    string msg = req.message();
	    MessageCommunication * br_msg( new MessageCommunication );
	    br_msg->set_message( msg );
	    br_msg->set_recipient( recipient );
	    br_msg->set_sender( sender.name );
	    ServerResponse all_res;
	    all_res.set_option( MESSAGECOMMUNICATION );
	    all_res.set_allocated_messagecommunication( br_msg );
	    
	    if (recipient == "everyone"){
	    /* Notify all users of message */
		    send_all( all_res, sender.name );
	    } else {
	    /* Send to one user */
	    	client_info rec;
	    	rec = get_user( recipient );
	    	send_response( rec.req_fd, &rec.socket_info, all_res );
	    }
	    
	    
	    ServerResponse res_r;
	    res_r.set_option( MESSAGECOMMUNICATION );
	    res_r.set_code( SUCCESS );
	    return res_r;
	}

        
        
        ServerResponse send_to_all( string msg );

        ServerResponse error_response( int option ) {
	    /* Building response */
	    fprintf(_log_level, "Error al realizar la respuesta al mensaje: %d\n", option);
	    
	    ServerResponse res;
	    res.set_option( option );
	    res.set_code( 500 );
	    return res;
	}

        
        string register_user( UserRegistration req, client_info cl ) {
	    fprintf(_log_level, "Registrando un nuevo usuario\n");
	    map<std::string, client_info> all_users = get_all_users();
	    /* Step 1: Register user and assign user id */

	    // Check if user name or ip is registered
	    fprintf(_log_level, "Revisando si el nombre de usuario ya existe..\n");
	    if( all_users.find( req.username() ) != all_users.end() ) {
		send_response( cl.req_fd, &cl.socket_info, error_response(USERREGISTRATION) );
		return "";
	    } /*else {
		fprintf(_log_level, "DEBUG: Checking if ip adddress is already connected to server\n");
		map<std::string, client_info>::iterator it;
		for( it = all_users.begin(); it != all_users.end(); it++ ) {
		    if( cl.ip == it->second.ip ) {
		        send_response( cl.req_fd, &cl.socket_info, error_response("Ip already in use") );
		        return "";
		    }
		}
	    }*/

	    // Adding mising data to client info
	    cl.name = req.username();
	    cl.status = "activo";

	    // Adding to db
	    fprintf(_log_level, "Usuario registrado: %s con: %d\n", req.username().c_str(), cl.req_fd);
	    add_user( cl );

	    /* Step 2: Return userid to client */
	    client_info conn_user = get_user(  req.username() );
	    
	    ServerResponse res;
	    res.set_option( USERREGISTRATION );
	    res.set_option( SUCCESS );
	    
	    send_response( conn_user.req_fd, &conn_user.socket_info, res );

	    fprintf(_log_level, "Cliente conectado correctamente.\n");
	    
	    return conn_user.name;
	}

        
        
        ServerResponse get_connected_users() {
	    /* Verify if there are connected users */
	    map<std::string, client_info> all_users = get_all_users();
	    if( all_users.empty() ) {
	    	return error_response(CONNECTEDUSERRESPONSE);
	    }

	    /* Get connected users */

	    map<std::string, client_info>::iterator it;
	    ConnectedUsersResponse * users( new ConnectedUsersResponse );
	    
	    //unsigned t1=clock();
	    // string inactivo = "inactivo";
	    for( it = all_users.begin(); it != all_users.end(); it++ ) {
		UserInfo * c_user_l = users->add_connectedusers();
		fprintf(_log_level, "Usuario conectado %s\n", it->first.c_str());
	    	// double time = (double(t1 - it->second.t0)/CLOCKS_PER_SEC);
	    	// double seconds = difftime(time(0), it->second.t0);
	    	// fprintf(_log_level, "Tiempo: %f\n", seconds);
		c_user_l->set_username(it->first);
		/* if (seconds > 60.0) {
		    c_user_l->set_status(inactivo);
		} else {
		    c_user_l->set_status(it->second.status);
		}*/
		c_user_l->set_status(it->second.status);
		c_user_l->set_ip(it->second.ip);
	    }

	    /* Form response */
	    ServerResponse res;
	    res.set_option( CONNECTEDUSERRESPONSE );
	    res.set_code( SUCCESS );
	    res.set_allocated_connectedusers( users );

	    return res;
	}

        
        
        ServerResponse change_user_status( ChangeStatus req, string name ) {
	    string new_st = req.status();
	    pthread_mutex_lock( &_user_list_mutex );
	    _user_list[ name ].status = new_st;
	    pthread_mutex_unlock( &_user_list_mutex );

	    ChangeStatus * ctr(new ChangeStatus);
	    ctr->set_username( name );
	    ctr->set_status( new_st );

	    ServerResponse res;
	    res.set_option( CHANGESTATUSRESPONSE );
	    res.set_code( SUCCESS );
	    res.set_allocated_change( ctr );
	    return res;
	}
	
	
	ServerResponse get_user_info( UserRequest req ) {
	    client_info rec;
	    if( req.has_user() ) {
		rec = get_user( req.user() );
	    }
	    
	    UserInfo * ctr(new UserInfo);
	    ctr->set_username( rec.name );
	    ctr->set_status( rec.status );
	    ctr->set_ip( rec.ip);

	    ServerResponse res;
	    res.set_option( USERINFORESPONSE );
	    res.set_code( SUCCESS );
	    res.set_allocated_userinforesponse( ctr );
	    return res;
	}

        
        ClientPetition parse_request( char *req ) {
	    ClientPetition cl_msg;
	    cl_msg.ParseFromString(req);
	    return cl_msg;
	}

        
        void send_all( ServerResponse res, string sender ) {
	    fprintf(_log_level, "Enviando solicitud a todos los usuarios conectados\n");

	    /* Iterate trough all connected users and send message */
	    map<std::string, client_info> all_users = get_all_users();
	    map<std::string, client_info>::iterator it;
	    for( it = all_users.begin(); it != all_users.end(); it++ ) {
		if( it->first != sender ) {
		    client_info user = it->second;
		    send_response( user.req_fd, &user.socket_info, res );
		}
	    }
	}

        
        static void * new_conn_h( void * context ) {
	    /* Get server context */
	    Server * s = ( ( Server * )context );

	    /* Getting thread ID*/
	    pid_t tid = gettid();
	    fprintf( s->_log_level, "Iniciando conexion con el thread: %d\n", ( int )tid);

	    /* Start processing connection */
	    struct client_info req_ds = s->req_pop();
	    char req[ MESSAGE_SIZE ];
	    s->read_request( req_ds.req_fd, req );
	    
	    /* New connection must be new user */
	    ClientPetition in_req = s->parse_request( req );

	    int in_opt = in_req.option();

	    string usr_nm;
	    unsigned t0, t1;

	    if( in_opt == 1 ) {
		usr_nm = s->register_user( in_req.registration(), req_ds );
	    } else {
		usr_nm = "";
		fprintf( s->_log_level, "No hay sido posible establecer nueva conexion con el thread: %d\n", ( int )tid);
		close( req_ds.req_fd );
		pthread_exit( NULL );
	    }

	    /* Server infinite loop */
	    if( usr_nm != "" ){
		fprintf( s->_log_level, "Escuchando mensajes del cliente '%s' en el thread: %d\n", usr_nm.c_str(), ( int )tid);
		client_info user_ifo = s->get_user( usr_nm );
		// user_ifo.id = req_ds.id;
		char req[ MESSAGE_SIZE ];
		while( 1 ) {
		    /* Read for new messages */
		    /*t1=clock();
		    double time = (double(t1-t0));
		    if (time > 30.0) {
		    	user_ifo.status = "inactivo";
		    }*/
		    // fprintf(s->_log_level, "Execution Time: %f\n", time);
		    int read_sz = s->read_request( req_ds.req_fd, req );
		
		
		    /* Check if it has error or is empty */
		    if( read_sz <= 0 ) {
		        /* Error will disconnect user */
		        fprintf(s->_log_level, "Error no ha sido posible leer la solicitud\n");
		        fprintf(s->_log_level,"Desconectando usuario %s con %d\n", usr_nm.c_str(), req_ds.req_fd);
		        s->delete_user( usr_nm );
		        fprintf(s->_log_level, "Cerrando al cliente...\n");
		        close( req_ds.req_fd ); 
		        break;
		    } 
		    time_t start = time(0);
		    /* Valid incomming request */
		    fprintf(s->_log_level, "Nueva solicitud del usuario %s con %d...\n", usr_nm.c_str(), req_ds.req_fd);
		    ServerResponse res = s->process_request( s->parse_request( req ), user_ifo );
		    
		    if( s->send_response( req_ds.req_fd, &req_ds.socket_info, res ) < -1 ) {
		        fprintf( s->_log_level, "Error al enviar la respuesta a %d\n", req_ds.req_fd );
		    } else {
		        fprintf(s->_log_level, "Enviando respuesta a: %d\n", req_ds.req_fd);
		        s->update_last_time(user_ifo);
		        s->check_status();
		        //t0=clock();
		    	//user_ifo.t0 = time(0);
		    	// clock_gettime(CLOCK_MONOTONIC, &user_ifo.start);
		    	// fprintf(s->_log_level, "tiempoooooooo: %f\n", difftime(time(0), start));
		    	//user_ifo.status = "inactivo";
		    }

		}
	    } else {
		close( req_ds.req_fd ); 
	    }
	    pthread_exit( NULL );
	}
	
	void update_last_time(client_info cl){
	    time_t actual;
	    pthread_mutex_lock( &_user_list_mutex );
	    _user_list[ cl.name ].start = time(&actual);
	    pthread_mutex_unlock( &_user_list_mutex );
	}

        
        
    private:
        pthread_mutex_t _req_queue_mutex;
        queue <client_info> _req_queue;
        pthread_mutex_t _user_list_mutex;
        map<std::string, client_info> _user_list;
        FILE *_log_level;
        
        
        client_info req_pop() {
	    client_info req;
	    pthread_mutex_lock( &_req_queue_mutex );
	    req = _req_queue.front();
	    _req_queue.pop();
	    pthread_mutex_unlock( &_req_queue_mutex );
	    return req;
	}

        
        
        void req_push( client_info el ) {
	    pthread_mutex_lock( &_req_queue_mutex );
	    _req_queue.push( el );
	    pthread_mutex_unlock( &_req_queue_mutex );
	}

        
        
        client_info get_user( string key ) {
	    client_info user;
	    pthread_mutex_lock( &_user_list_mutex );
	    user = _user_list[ key ];
	    pthread_mutex_unlock( &_user_list_mutex );
	    return user;
	}

        
        
        void add_user( client_info el ) {
	    pthread_mutex_lock( &_user_list_mutex );
	    _user_list[ el.name ] = el;
	    pthread_mutex_unlock( &_user_list_mutex );
	}

        
        
        void delete_user( string key ) {
	    pthread_mutex_lock( &_user_list_mutex );
	    _user_list.erase( key );
	    pthread_mutex_unlock( &_user_list_mutex );
	}

        
        map<std::string, client_info> get_all_users() {
	    map<std::string, client_info> tmp;
	    pthread_mutex_lock( &_user_list_mutex );
	    tmp = _user_list;
	    pthread_mutex_unlock( &_user_list_mutex );
	    return tmp;
	}
    
};



int main(int argc, char *argv[]) {

    GOOGLE_PROTOBUF_VERIFY_VERSION;

    if( argc < 2) {
        perror("Falto agregar como parametro el puerto");
        return -1;
    }

    
    int port = atoi(argv[1]);
    Server server(port);
    
    if( server.initiate() < 0 ) {
        perror("No se ha podido iniciar :(");
        return -1;
    }

    server.start();

}


