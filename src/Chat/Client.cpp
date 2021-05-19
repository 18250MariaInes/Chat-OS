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
#include <signal.h>

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

#ifndef connected_user
struct connected_user {
    string name;
    string status;
    string ip;
};
#endif

#ifndef message_type
enum message_type {
    BROADCAST,
    DIRECT
};
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

class Client {
public:
	int _user_id;
        int _sock;
        struct sockaddr_in _serv_addr;
        char * _username;
        int _error = 0;
        struct connected_user c_usr1;
        Client( char * username, FILE *log_level ) {
	    _sock = -1;
	    _close_issued = 0;
	    pthread_mutex_init(  &_noti_queue_mutex, NULL );
	    pthread_mutex_init( &_stop_mutex, NULL );
	    pthread_mutex_init( &_connected_users_mutex, NULL );
	    pthread_mutex_init( &_error_queue_mutex, NULL );
	    _username = username;
	    _log_level = log_level;
	}

        int connect_server(char *server_address, int server_port) {
	    /* Create socket */
	    fprintf(_log_level,"Creando socket...\n");
	    if( ( _sock = socket(AF_INET, SOCK_STREAM, 0) ) < 0 ) {
		fprintf(_log_level,"Error en la creacion del socket");
		return -1;
	    }

	    /* Save server info on ds*/
	    fprintf(_log_level, "Guardando la informacion del servidor\n");
	    _serv_addr.sin_family = AF_INET; 
	    _serv_addr.sin_port = htons(server_port); // Convert to host byte order
	    if(  inet_pton(AF_INET, server_address, &_serv_addr.sin_addr) <= 0 ) { // Convert ti network address
		fprintf(_log_level,"Error direccion invalida del servidor\n");
		return -1;
	    }

	    /* Set up connection */
	    fprintf(_log_level,"Configurando la conexion\n");
	    if( connect(_sock, (struct sockaddr *)&_serv_addr, sizeof(_serv_addr)) < 0 ) {
		fprintf(_log_level,"Error no ha sido posible establecer una conexion con el servidor\n");
		return -1;
	    }

	    return 0;
	}

        int log_in() {

	    /* Step 1: Sync option 1 */
	    fprintf(_log_level,"Solicitud de inicio de sesion\n");
	    UserRegistration * my_info(new UserRegistration);
	    my_info->set_username(_username);

	    ClientPetition msg;
	    msg.set_option( SYNCHRONIZED );
	    msg.set_allocated_registration(my_info);

	    /* Sending sync request to server */
	    send_request( msg );

	    /* Step 2: Read ack from server */
	    fprintf(_log_level,"Esperando por el servidor\n");
	    char ack_res[ MESSAGE_SIZE ];
	    int rack_res_sz = read_message( ack_res );
	    ServerResponse res = parse_response( ack_res );

	    if( res.code() == FAIL ) {
		return -1;
	    } 
	    _user_id = 1;

	    return 0;
	}
	
	
	int get_user_info( string name ){
	    UserRequest * my_info(new UserRequest);
	    my_info->set_user( name );

	    ClientPetition msg;
	    msg.set_option( ACKNOWLEDGE );
	    msg.set_allocated_users(my_info);

	    if( send_request( msg ) < 0 ) {
		fprintf( _log_level, "No ha sido posible enviar su solicitud\n" );
	    }

	    return 0;
	}

        int get_connected_request() {
	    /* Build request */
	    fprintf(_log_level,"Haciendo solicitud de usuarios conectados\n");
	    
	    ClientPetition req;
	    req.set_option( CONNECTEDUSER );
	    
	    if( send_request( req ) < 0 ) {
		fprintf( _log_level, "No ha sido posible enviar su solicitud\n" );
	    }

	    return 0;
	}

        
        int send_request(ClientPetition request) {
	    /* Serealize string */
	    int res_code = request.option();
	    fprintf(_log_level,"Solicitud con opcion: %d\n", res_code);
	    std::string srl_req;
	    request.SerializeToString(&srl_req);

	    char c_str[ srl_req.size() + 1 ];
	    strcpy( c_str, srl_req.c_str() );

	    /* Send request to server */
	    fprintf(_log_level,"Enviando solicitud...\n");
	    if( sendto( _sock, c_str, strlen(c_str ), 0, (struct sockaddr*)&_serv_addr,sizeof( &_serv_addr ) ) < 0 ) {
		fprintf(_log_level,"Error enviando solicitud");
		return -1;
	    }

	    fprintf(_log_level,"Su solicitud ha sido enviada con exito %d\n", _sock);
	    return 0;
	}

        
        int broadcast_message( string msg, string dest_nm ) {
	    MessageCommunication * br_msg( new MessageCommunication );
	    br_msg->set_message( msg );
	    if( dest_nm != "" ) {
		br_msg->set_recipient( dest_nm );
	    }
	    ClientPetition req;
	    req.set_option( DMMESSAGE );
	    req.set_allocated_messagecommunication( br_msg );

	    if( send_request( req ) < 0 ) {
		fprintf( _log_level, "Error no ha sido posible enviar su solicitud\n" );
	    }

	    return 0;

	}

        
       
        ServerResponse parse_response( char *res ) {
	    ServerResponse response;
	    response.ParseFromString(res);
	    return response;
	 }

        
        int change_status( string n_st ) {
	    ChangeStatus * n_st_res( new ChangeStatus );
	    n_st_res->set_status( n_st );
	    ClientPetition req;
	    req.set_option( CHANGESTATUS );
	    req.set_allocated_change( n_st_res );
	    
	    if( send_request( req ) < 0 ) {
		fprintf( _log_level, "Error no ha sido posible enviar su solicitud\n" );
	    }

	    return 0;

	}
	
	void set_user_info(UserInfo res){
	    c_usr1.name = res.username();
	    c_usr1.status = res.status();
	    c_usr1.ip = res.ip();
	}

        
	int read_message( void *res ) {
	    /* Read for server response */
	    int rec_sz;
	    fprintf(_log_level, "Esperando por mensajes del servidor en %d\n", _sock);
	    if( ( rec_sz = recvfrom(_sock, res, MESSAGE_SIZE, 0, NULL, NULL) ) < 0 ) {
		fprintf(_log_level,"Ha ocurrido un error al leer la respuesta");
		exit(EXIT_FAILURE);
	    }
	    fprintf(_log_level, "Mensaje del servidor recibido\n");
	    return rec_sz;
	}


        
        int process_response( ServerResponse res );
        
        
        void start_session() {
	    /* Verify a connection with server was stablished */
	    if ( _sock < 0 ){
		fprintf(_log_level, "Error no ha sido posible conectar con el servidor\n" );
		exit( EXIT_FAILURE );
	    }

	    /* Verify if client was registered on server */
	    if( _user_id < 0 ) { // No user has been registered
		/* Attempt to log in to server */
	    	if( log_in() < 0 ) {
		    fprintf(_log_level, "Error no ha sido posible iniciar sesion\n");
		    exit( EXIT_FAILURE );
		} else {
		    fprintf(_log_level, "Ha iniciado sesion\n");
		}
	    }
	    /* Create a new thread to listen for messages from server */
	    pthread_t thread;
	    pthread_create( &thread, NULL, &bg_listener, this );

	    /* Start client interface */
	    int no = 0;
	    string all = "everyone";
	    message_type msg_t;
	    while(1) {

		if( no ) {
		    string err;

		    if( _error < 0 ) {
		        cout << "Un error inesperado ha ocurrido " << endl;
		    } else if( no == 1 ) {
		        message_received mtp;
		        while( pop_to_buffer( msg_t, &mtp ) == 0 ) {
		            cout << "........ Mensajes no leidos ........" << endl;
		            cout << "Nombre de usuario: " << mtp.from_username << endl;
		            cout << "Mensaje: " << mtp.message << endl;
		            cout << ".................................." << endl;
		        }
		    } else if( no == 2 ) {
		        map <string, connected_user> tmp = get_connected_users();
		        map<string, connected_user>::iterator it;
		        cout << "........ Usuarios conectados ........" << endl;
		        for( it = tmp.begin(); it != tmp.end(); it++ ) {
		            cout << "Nombre de usuario: " << it->first << endl;
		            cout << "Estado actual: " << it->second.status << endl;
		            cout << ".................................." << endl;
		        }
		    } else if(no == 3) {
		    	cout << "--------------------------------------" << endl;
		        cout << "Nombre de usuario: " << c_usr1.name << endl;
		        cout << "Estado actual: " << c_usr1.status << endl;
		        cout << "IP del usuario: " << c_usr1.ip << endl;
		        cout << "--------------------------------------" << endl;
		    }
		}
		
		int input;
		printf("\n\nTe damos la bienvenida, %s. Esperamos hayas traido pizza \n", _username);
		printf("\t1. Enviar mensaje a #general\n");
		printf("\t2. Enviar mensaje privado \n");
		printf("\t3. Cambiar tu estado \n");
		printf("\t4. Ver usuarios activos \n");
		printf("\t5. Ver perfil de usuario \n");
		printf("\t6. Ver #general \n");
		printf("\t7. Ver mensajes privados \n");
		printf("\t8. Ayuda \n");
		printf("\t9. Finalizar sesion \n");
		printf("Ingresa una opcion: \n");
		cin >> input;
		string t;
		getline(cin, t);
		
		string br_msg = "";
		string dm = "";
		string dest_nm = "";
		int res_cd;
		no = 0;
		int st;
		string n_sts = "activo";
		int mm_ui, usr_id;
		string usr;
		connected_user c_usr;
		int res_gi;
		switch ( input ) {
		    case 1:
		        cout << "Ingrese el mensaje a enviar: \n";
		        getline( cin, br_msg );
		        res_cd = broadcast_message( br_msg, all );
		        break;
		    case 2:
		        printf("Ingrese el nombre de usuario: \n");
		        getline( cin, dest_nm );
		        printf("Ingrese el mensaje a enviar: \n");
		        getline( cin, dm );
		        res_cd = broadcast_message( dm, dest_nm );
		        break;
		    case 3:
		        printf("Seleccione su nuevo estado actual: \n");
		        printf("1. Activo\n");
		        printf("2. Inactivo\n");
		        printf("3. Ocupado\n");
		        cin >> st;
		        getline(cin, t);
		        if(st == 1)
		            n_sts = "activo";
		        else if( st == 2 )
		            n_sts = "inactivo";
		        else if( st == 3 )
		            n_sts = "ocupado";
		        res_cd = change_status( n_sts );
		        break;
		    case 4:
		        res_cd = get_connected_request();
		        no = 2;
		        break;
		    case 5:
		        printf("Ingresa el nombre de usuario:\n");
	                getline(cin, usr);
	                res_gi = get_user_info( usr );
	                no = 3;
		        break;
		    case 6:
		        msg_t = BROADCAST;
		        no = 1;
		        break;
		    case 7:
		        msg_t = DIRECT;
		        no = 1;
		        break;
		    case 8:
		    	cout << "--------------------------------------" << endl;
		        cout << "Enviar mensaje a #general presione 1" << endl;
		        cout << "Cuando se le indique, escriba unicamente su mensaje" << endl;
		        cout << "Presiona ENTER y el mensaje sera enviado a todos los usuarios conectados" << endl;
		        cout << "--------------------------------------" << endl;
		        cout << "Enviar mensaje privado presione 2" << endl;
		        cout << "Cuando se le indique, escriba el nombre de usuario del destinatario" << endl;
		        cout << "Cuando se le indique, escriba su mensaje" << endl;
		        cout << "Presiona ENTER y el mensaje sera enviado al usuario indicado" << endl;
		        cout << "--------------------------------------" << endl;
		        cout << "Para ver sus mensajes no leidos de #general presione 6" << endl;
		        cout << "--------------------------------------" << endl;
		        cout << "Para ver sus mensajes privados no leidos presione 7" << endl;
		        cout << "--------------------------------------" << endl;
		        cout << "Para SOPORTE TECNICO contacte a:" << endl;
		        cout << ">>> Camila Gonzalez: gon18398@uvg.edu.gt" << endl;
		        cout << ">>> Maria Ines Vasquez: vas18250@uvg.edu.gt" << endl;
		        cout << ">>> Diana de Leon: dele18607@uvg.edu.gt" << endl;
		        break;
		    case 9:
		        break;
		    default:
		        printf("Opcion no valida\n");
		        break;
		}
		sleep( 2 );
		if( input == 9 )
		    break;
	    }
	    stop_session();
	}
        
        
        void stop_session() {
	    fprintf(_log_level, "Terminando la sesion...\n");
	    close( _sock ); // Stop send an write operations
	    send_stop(); // Set stop flag
	}
	
	
	void handle_error(){
	    _error = -1;   
	}
        
        
        static void * bg_listener( void * context ) {
	    /* Get Client context */
	    Client * c = ( ( Client * )context );

	    pid_t tid = gettid();
	    fprintf(c->_log_level, "Iniciando cliente con el thread: %d\n", ( int )tid);

	    /* read for messages */
	    while( c->get_stopped_status() == 0 ) {
		char ack_res[ MESSAGE_SIZE ];
		if( c->read_message( ack_res ) <= 0 ) {
		    fprintf( c->_log_level, "El servidor ha sido desconectado, terminando la sesion..." );
		    c->send_stop();
		    break;
		}
		fprintf(c->_log_level,"Hay nuevos mensajes del servidor\n");
		ServerResponse res = c->parse_response( ack_res );
		
		if (res.code() == 500){
		    c->handle_error();
		}
		
		/* Process acoorging to response, we ignore status responses */
		switch ( res.option() ) {
		    case DMMESSAGE:
		        c->push_res( res ); 
		        break;
		    case CONNECTEDUSERRESPONSE:
		        c->parse_connected_users( res.connectedusers() );
		        break;
		    case USERINFORESPONSE:
		        c->set_user_info( res.userinforesponse() );
		        break;
		    default:
		        break;
		}
	    }
	    pthread_exit( NULL );
	}

    private:
        FILE *_log_level;
        pthread_mutex_t _noti_queue_mutex;
        queue <message_received> _dm_queue;
        queue <message_received> _br_queue;
        pthread_mutex_t _connected_users_mutex;
        map <string, connected_user> _connected_users;
        
        map <string, connected_user> parse_connected_users( ConnectedUsersResponse c_usr ) {
	    /* Save connected users */
	    map <string, connected_user> c_users;
	    int i;
	    for( i = 0; i < c_usr.connectedusers().size(); i++ ) {
		UserInfo rec_user = c_usr.connectedusers().at( i );
		struct connected_user lst_users;
		lst_users.name = rec_user.username();
		lst_users.status = rec_user.status();
		lst_users.ip = rec_user.ip();
		c_users[ rec_user.username() ] = lst_users;
	    }
	    set_connected_users( c_users );
	    return c_users;
	}

        
        map <string, connected_user> get_connected_users() {
	    map <string, connected_user> tmp;
	    pthread_mutex_lock( &_connected_users_mutex );
	    tmp = _connected_users;
	    pthread_mutex_unlock( &_connected_users_mutex );
	    return tmp;
	}

        
        void set_connected_users( map <string, connected_user> cn_u ) {
	    pthread_mutex_lock( &_connected_users_mutex );
	    _connected_users = cn_u;
	    pthread_mutex_unlock( &_connected_users_mutex );
	}

        
        pthread_mutex_t _error_queue_mutex;
        
        int _close_issued;
        pthread_mutex_t _stop_mutex;
        
        int get_stopped_status() {
	    int tmp = 0;
	    pthread_mutex_lock( &_stop_mutex );
	    tmp = _close_issued;
	    pthread_mutex_unlock( &_stop_mutex );
	    return tmp;
	}

        
        void send_stop() {
	    pthread_mutex_lock( &_stop_mutex );
	    _close_issued = 1;
	    pthread_mutex_unlock( &_stop_mutex );
	    fprintf(_log_level, "Mensaje de desconectado enviado correctamente\n");
	}

        
        
        int pop_to_buffer( message_type mtype, message_received * buf ) {
	    int res;
	    pthread_mutex_lock( &_noti_queue_mutex );
	    if( mtype == BROADCAST ) {
		fprintf(_log_level, "Verificando si hay nuevos mensajes en canal general\n");
		if( !_br_queue.empty() ) {
		    fprintf(_log_level, "Hay mensajes nuevos, obteniendo...\n");
		    *buf = _br_queue.front();
		    _br_queue.pop();
		    res = 0;
		} else {
		    fprintf(_log_level, "Estas al dia, no hay mensajes nuevos\n");
		    res = -1;
		}
	    } else if( mtype == DIRECT ) {
		fprintf(_log_level, "Verificando si hay nuevos mensajes privados\n");
		if( !_dm_queue.empty() ) {
		    fprintf(_log_level, "Hay mensajes nuevos, obteniendo...\n");
		    *buf = _dm_queue.front();
		    _dm_queue.pop();
		    res = 0;
		} else {
		    fprintf(_log_level, "Estas al dia, no hay mensajes nuevos\n");
		    res = -1;
		}
	    }
	    pthread_mutex_unlock( &_noti_queue_mutex );
	    return res;
	}

        
        
        void push_res( ServerResponse el ) {
	    int option = el.option();
	    message_received msg;
	    pthread_mutex_lock( &_noti_queue_mutex );
	    if( option == 4 ) {
		msg.from_username = el.messagecommunication().sender();
		msg.message = el.messagecommunication().message();
		msg.to_username = el.messagecommunication().recipient();
		if (msg.to_username == "everyone") {
			_br_queue.push( msg );
		} else if (msg.to_username == _username) {
			_dm_queue.push( msg );
		}
	    } 
	    pthread_mutex_unlock( &_noti_queue_mutex );
	}
};


int running = 1;

void handle_shutdown( int signal ) {
    running = 0;
}

int main(int argc, char *argv[]) {

    signal(SIGINT, handle_shutdown);

    if(argc < 4) {
        printf("Debe ingresar nombre de usuario, ip y el puerto\n");
        return 1;
    }

    FILE * log_file = fopen("client.log", "w");

    /* Create client with username */
    Client client( argv[1], log_file );

    /* Connect to server on address:port*/
    int port = atoi(argv[3]);
    char *address = argv[2];

    if( client.connect_server(address, port) < 0 ) {
        printf("Error de conexion\n");
        return -1;
    }

    printf("Conectando al servidor con %s:%d\n", address, port);

    /* Log in to server */
    printf("Iniciando sesion de: %s...\n", argv[1]);
    if( client.log_in() < 0 ) {
        printf("No ha sido posible iniciar sesion\n");
        return -1;
    }

    client.start_session();

    if( !running ) {
        client.stop_session();
        fclose( log_file );
    }


    return 0;

}

