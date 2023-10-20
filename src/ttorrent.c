// Trivial Torrent

// TODO: some includes here

#include "file_io.h"
#include "logger.h"
#include <errno.h>
#include <stdlib.h>
// TODO: hey!? what is this?

/**
 * This is the magic number (already stored in network byte order).
 * See https://en.wikipedia.org/wiki/Magic_number_(programming)#In_protocols
 */
static const uint32_t MAGIC_NUMBER = 0xde1c3231; // = htonl(0x30321cde);

static const uint8_t MSG_REQUEST = 0;
static const uint8_t MSG_RESPONSE_OK = 1;
static const uint8_t MSG_RESPONSE_NA = 2;

enum { RAW_MESSAGE_SIZE = 13 };


int recv_all(int socket, void *buffer, size_t length)// trobat a https://stackoverflow.com/questions/13479760/c-socket-recv-and-send-all-data, doncs la funció recv em donava errors de pèrdua de dades.
{
    int size = 0;
    char *ptr = (char*) buffer;
    while (length > 0)
    {
        int i = recv(socket, ptr, length, 0);
        if (i < 1) return i;
        ptr += i;
        length -= i;
        size += i;
    }
    return size;
}

    
int main(int argc, char **argv) {

	set_log_level(LOG_DEBUG);

	log_printf(LOG_INFO, "Trivial Torrent (build %s %s) by %s", __DATE__, __TIME__, "A. Juárez");
// --------------------------------------------------------SERVER
	if (argc == 4 && strcmp(argv[1], "-l") == 0) {
		// -----------------------seteo variables
		struct torrent_t ttorrent2;
		int32_t puerto = atoi(argv[2]);
		char* filename = "test_file_server";

		struct sockaddr_in adr;
		memset(&adr, 0, sizeof(struct sockaddr_in));
		adr.sin_family = AF_INET;
		adr.sin_addr.s_addr = INADDR_ANY;
		adr.sin_port = htons(puerto);

		if (create_torrent_from_metainfo_file(argv[3], &ttorrent2, filename)) {
			log_printf(LOG_DEBUG, "Error al crear el torrent: %s", strerror(errno));
			errno = 0;
			exit(EXIT_FAILURE);
		}
		// -----------------------creación del socket

		int socket2 = socket(AF_INET, SOCK_STREAM, 0);
		if (socket2 < 0) {
			log_printf(LOG_DEBUG, "Error al crear el Socket: %s", strerror(errno));
			errno = 0;
		}
		else log_printf(LOG_INFO, "SOCKET creado exitosamente");

		if (bind(socket2, (struct sockaddr*) & adr, sizeof(adr))) {
			log_printf(LOG_DEBUG, "Error en el Bind: %s", strerror(errno));
			errno = 0;
		}
		else log_printf(LOG_INFO, "BIND correcto");

		if (listen(socket2, 30)) {
			log_printf(LOG_DEBUG, "Error en el Listen: %s", strerror(errno));
			errno = 0;
		}
		else log_printf(LOG_INFO, "LISTEN correcto");

		// ---------------------recv y send

		while (1) {
			struct sockaddr_in cliente;
			unsigned int size = sizeof(struct sockaddr_in);
			int recv = accept(socket2, (struct sockaddr_in*) & cliente, &size);

			if (recv < 0) {
				log_printf(LOG_DEBUG, "Error en el Accept: %s %i", strerror(errno), socket2);
				errno = 0;
				continue;
			}
			else log_printf(LOG_INFO, "ACCEPT OK");

			if (fork() == 0) { //Proceso hijo
				close(socket2);
				while (1) {
					char buffer[RAW_MESSAGE_SIZE];
					int MsgRecibido = recv_all(recv, &buffer, RAW_MESSAGE_SIZE);
					uint32_t magic;
					uint8_t code;
					uint64_t number;

					if (MsgRecibido == 0) {  //Si el cliente no envía datos se intenta realizar otra conexión
						break;
					}

					if (MsgRecibido < 0) {
						log_printf(LOG_DEBUG, "No se han recibido datos: %s %i", strerror(errno), recv);
						errno = 0;
					}
					else {
						memcpy(&magic, buffer, sizeof(MAGIC_NUMBER));
						memcpy(&code, buffer + sizeof(MAGIC_NUMBER), sizeof(MSG_REQUEST));
						memcpy(&number, buffer + sizeof(MAGIC_NUMBER) + sizeof(MSG_REQUEST), sizeof(number));
						log_printf(LOG_INFO, "Se han recibido %i datos", MsgRecibido);
						log_printf(LOG_INFO, "magic: %x  code: %i block: %i  torrent: %d",
							magic, code, number, ttorrent2.block_map[number]);

						if (magic == MAGIC_NUMBER && code == MSG_REQUEST) {
							log_printf(LOG_INFO, "Magic number coincide y hay msg request");

							if (ttorrent2.block_map[number]) {
								struct block_t bloque;
								char buffEnvio[MAX_BLOCK_SIZE + RAW_MESSAGE_SIZE];
								memcpy(buffEnvio, &MAGIC_NUMBER, sizeof(MAGIC_NUMBER));
								memcpy(buffEnvio + sizeof(MAGIC_NUMBER), &MSG_RESPONSE_OK, sizeof(MSG_RESPONSE_OK));
								memcpy(buffEnvio + sizeof(MAGIC_NUMBER) + sizeof(MSG_REQUEST), &number, sizeof(number));

								//carga y check
								if (load_block(&ttorrent2, number, &bloque) < 0) {
									log_printf(LOG_INFO, "Error al cargar el bloque: %s", strerror(errno));
									errno = 0;
								}
								else log_printf(LOG_INFO, "Bloque cargado");

								memcpy(buffEnvio + RAW_MESSAGE_SIZE, &bloque.data, bloque.size);

								//envío y check
								if (send(recv, &buffEnvio, RAW_MESSAGE_SIZE + bloque.size, 0) < 0) {
									log_printf(LOG_INFO, "Error en el send: %s", strerror(errno));
									errno = 0;
								}
								else log_printf(LOG_INFO, "Datos enviados!");

							}
							else {
								log_printf(LOG_INFO, "No se dispone de los datos del bloque %i", number);
								char buffEnvioF[RAW_MESSAGE_SIZE];
								memcpy(buffEnvioF + sizeof(MAGIC_NUMBER), &MSG_RESPONSE_NA, sizeof(MSG_RESPONSE_NA));

								if (send(recv, &buffEnvioF, RAW_MESSAGE_SIZE, 0) < 0) {
									log_printf(LOG_INFO, "No se ha podido avisar al cliente de la falta de datos: %s", strerror(errno));
									errno = 0;
								}
								else log_printf(LOG_INFO, "Enviando mensaje vacío :(");

							}
						}
					}
				}
				log_printf(LOG_INFO, "¡El cliente ya dispone de todos los datos!");
				close(recv);
				exit(EXIT_SUCCESS);
			}
			else {
				//proceso padre
				close(recv);
			}
		}
		close(socket2);
		if (destroy_torrent(&ttorrent2) < 0) {
			log_printf(LOG_INFO, "Error al eliminar el torrent: %s", strerror(errno));
			errno = 0;
		}
	}
//---------------------------------------------------------FINAL SERVER

//---------------------------------------------------------CLIENTE
  if (argc == 2){
  
// -------------------------------------Variables
    struct torrent_t ttorrent1;
    struct sockaddr_in adr;
    memset(&adr,0,sizeof(struct sockaddr_in));
    char* name = "test_file";
    log_printf(LOG_INFO, "name: %s",argv[1]);

	if (create_torrent_from_metainfo_file(argv[1], &ttorrent1, name ) == -1) {
        log_printf(LOG_DEBUG, "Error en la ejecución de la estructura torrent");
    } 
	else {
		log_printf(LOG_INFO, "ttorrent generado con éxito");
	}

    for ( uint64_t i = 0; i < ttorrent1.peer_count; i++) {//se realiza conexión de cada peer
      int socket1 = socket(AF_INET, SOCK_STREAM, 0);
      adr.sin_family = AF_INET;
      adr.sin_addr.s_addr = ttorrent1.peers[i].peer_address[0] << 24 | 
                            ttorrent1.peers[i].peer_address[1] << 16 | 
                            ttorrent1.peers[i].peer_address[2] << 8 | 
                            ttorrent1.peers[i].peer_address[3] << 0;
      adr.sin_port = ttorrent1.peers[i].peer_port;
      adr.sin_addr.s_addr = htonl(adr.sin_addr.s_addr);
      
// ------------------------------------Conectar al Server     
      if (connect(socket1, (struct sockaddr *)&adr, sizeof(adr)) == -1){
			log_printf(LOG_DEBUG, "Error realizando el connect al puerto: %i", 8080+i);
      }
	  else {
			log_printf(LOG_INFO, "Connect correcto (%i)", 8080 + i);
	  

// ------------------------------------rcv y send           
        for (uint64_t j = 0; j < ttorrent1.block_count; j++) {//Bloques de información
            if(ttorrent1.block_map[j] == 0) {//Si está vacío
                struct block_t bloque;
                uint8_t buffer[RAW_MESSAGE_SIZE] = {0};
                uint8_t server[RAW_MESSAGE_SIZE + MAX_BLOCK_SIZE] = {0};
                memcpy(buffer, &MAGIC_NUMBER, sizeof(MAGIC_NUMBER));
  				memcpy(buffer + sizeof(MAGIC_NUMBER), &MSG_REQUEST, sizeof(MSG_REQUEST));
  				memcpy(buffer + sizeof(MAGIC_NUMBER) + sizeof(MSG_REQUEST), &j, sizeof(j));
                uint64_t size = get_block_size(&ttorrent1, j);
                bloque.size = size;
                log_printf(LOG_INFO, "Pidiendo (magic: %x, code: %i, block: %i)", MAGIC_NUMBER,MSG_REQUEST,j);

                if (send(socket1, &buffer, sizeof(buffer), 0) <0){
                    log_printf(LOG_DEBUG, "Error en el send: %s",strerror(errno));
                    errno=0;
                }
                else log_printf(LOG_INFO, "Se ha enviado una petición para el bloque %i", j);

                int recv;

                if ((recv = recv_all(socket1, &server, RAW_MESSAGE_SIZE+bloque.size)) == 0)  {
                  log_printf(LOG_INFO,"El servidor no está enviando datos");
                  continue;
                } 

                if (recv < 0) {
                  log_printf(LOG_DEBUG, "Error en el recv: %s",strerror(errno));
                  errno=0;                  
                }
                else log_printf(LOG_INFO, "Se han recibido %i datos del bloque %i", size, j);
                
        // ---------------------------------Almacenar datos
            
                uint8_t mc_serv;
                memcpy(&mc_serv, server + sizeof(MAGIC_NUMBER), sizeof(mc_serv));

                if (mc_serv == MSG_RESPONSE_OK) {
                    memcpy(bloque.data, server +  sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint64_t), bloque.size);

                    if (store_block(&ttorrent1, j, &bloque)==-1){
                        log_printf(LOG_DEBUG, "Error al cargar el bloque: %s", strerror(errno));
                        errno=0;
                    } 
                    else log_printf(LOG_INFO, "Se han cargado los datos en el bloque");                  
                }
                else if (mc_serv == MSG_RESPONSE_NA) {log_printf(LOG_INFO, "El servidor no tiene los datos del bloque %i", j);}  
            }
			else {
				log_printf(LOG_INFO, "Ya se disponía de los datos del bloque %i", j);
			}
        }
		int condition = 0;
		int i=0;
	    while(condition==0 && i<ttorrent1.block_count){
	        if(ttorrent1.block_map[i] == 0){
				condition=1;
			}
	        i++;
	    }
	    if(condition==1){
	        log_printf(LOG_INFO, "No se tienen TODOS los datos");
	    }
	    else{
	        log_printf(LOG_INFO, "TODO OKAY");
			close(socket1);
			exit(EXIT_SUCCESS);
	    }
	  }
	 close(socket1);
	}
	exit(EXIT_FAILURE);
  }
  //---------------final client  

  (void)argc;
  (void)argv;
  (void)MAGIC_NUMBER;
  (void)MSG_REQUEST;
  (void)MSG_RESPONSE_NA;
  (void)MSG_RESPONSE_OK;

  return 0;
}
