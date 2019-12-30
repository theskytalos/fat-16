/*INCLUDE*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>

/*DEFINE*/
#define SECTOR_SIZE		512
#define CLUSTER_SIZE		2 * SECTOR_SIZE
#define ENTRY_BY_CLUSTER 	CLUSTER_SIZE / sizeof(dir_entry_t)
#define NUM_CLUSTER		4096
#define fat_name		"fat.part"
#define MAX_CMD_SIZE		4096

/*DIR NAVIGATOR*/
#define INVALID_DIR 	1
#define NOT_FOUND_DIR 	2
#define NOT_FOUND_FILE	3
#define NOT_A_DIR	4
#define NOT_A_FILE	5
#define FULL_DIR	6
#define NOT_EMPTY_DIR	7
#define ALREADY_EXISTS	8
#define BLOATED_SYSTEM	9
#define ROOT_DIR 	10
#define DATA_DIR	11

#define SUB_DIR 	1
#define FILE_DIR 	2

#define NAV_READ 	1
#define NAV_CREATE 	2
#define NAV_DELETE 	3

struct _dir_entry_t
{
	unsigned char filename[18];
	unsigned char attributes;
	unsigned char reserved[7];
	unsigned short first_block;
	unsigned int size;
};

typedef struct _dir_entry_t  dir_entry_t;

union _data_cluster
{
	dir_entry_t dir[CLUSTER_SIZE / sizeof(dir_entry_t)];
	uint8_t data[CLUSTER_SIZE];
};

typedef union _data_cluster data_cluster;

/*DATA DECLARATION*/
unsigned short fat[NUM_CLUSTER];
unsigned char boot_block[CLUSTER_SIZE];
dir_entry_t root_dir[32];
data_cluster clusters[4086];

bool is_fs_loaded;

/*FUNCTION DECLARATION*/
void command_interpreter(char*);
void explode_command(char*, char***, unsigned*);
void explode_directory(char*, char***, unsigned*);
void get_input_string(char*, char**);
bool validate_directory(char**, unsigned);
bool directory_navigator(char**, unsigned, unsigned*, unsigned*, unsigned*, unsigned);
bool create_file(char**, unsigned, unsigned*, unsigned);
bool write_file(char**, unsigned, unsigned, char*, unsigned*);
bool append_file(char**, unsigned, unsigned, char*, unsigned*);
bool read_file(char**, unsigned, unsigned, char**, unsigned*);
unsigned get_available_cluster();
void init(void);
void load();
void save();
data_cluster get_data_cluster(unsigned);
void save_data_cluster(unsigned, data_cluster);
bool check_file_existence();
void free_structure(char***, unsigned);

int main(int argc, char** argv)
{
	is_fs_loaded = false;
	char command[MAX_CMD_SIZE];
	while (true)
	{
		fprintf(stdout, ">> ");
		fgets(command, MAX_CMD_SIZE, stdin);
		if (strcmp(command, "\n") != 0) // Ignora comandos vazios.
			command_interpreter(command);
	}

	return EXIT_SUCCESS;
}

void command_interpreter(char* command)
{
	bool end_shell = false;

	char** command_pieces = NULL;
	unsigned command_pieces_size = 0;

	char** directory_pieces = NULL;
	unsigned directory_pieces_size = 0;

	char* input_string = NULL;
	// Preenche input_string com a string de entrada de dados, caso haja (write, append).
	get_input_string(command, &input_string);

	// Quebra o comando em partes delimitadas por ' '.
	explode_command(command, &command_pieces, &command_pieces_size);

	if (!is_fs_loaded)
	{
		if (strcmp(command_pieces[0], "init") != 0 && strcmp(command_pieces[0], "load") != 0 && strcmp(command_pieces[0], "exit") != 0)
		{
			fprintf(stderr, "O sistema de arquivos não está carregado.\n");
			free_structure(&command_pieces, command_pieces_size);
			free(input_string);
			return;
		}
	}

	// Caso o comando lide com diretórios.
	if (command_pieces_size > 1)
	{
		// Separa o diretório encontrado por '/'.
		explode_directory(command_pieces[command_pieces_size - 1], &directory_pieces, &directory_pieces_size);

		// Averigua se o diretório não é inválido. Caso seja, libera memória das estruturas utilizadas e volta para o main.
		if (!validate_directory(directory_pieces, directory_pieces_size))
		{
			fprintf(stderr, "Diretório inválido.\n");
			free_structure(&command_pieces, command_pieces_size);
			free_structure(&directory_pieces, directory_pieces_size);
			free(input_string);
			return;
		}
	}

	/* RECONHECIMENTO DOS COMANDOS */
	if (strcmp(command_pieces[0], "init") == 0)
	{
		init();
		save();
	}
	else if (strcmp(command_pieces[0], "load") == 0)
	{
		if (check_file_existence())
		{
			load();
			is_fs_loaded = true;
		}
		else
			fprintf(stderr, "Arquivo %s não encontrado.\n", fat_name);
	}
	else if (strcmp(command_pieces[0], "ls") == 0)
	{
		if (command_pieces_size > 0)
		{
			// Se apenas 'ls' for passado, cria um diretório '/' para que o root_dir seja listado.
			if (command_pieces_size == 1)
			{
				directory_pieces = (char**) malloc(1 * sizeof(char*));
				directory_pieces[0] = (char*) malloc(2 * sizeof(char));
				directory_pieces_size = 1;
				strcpy(directory_pieces[0], "/");
			}

			unsigned index = 0, return_info = 0, type = 0;

			if (directory_navigator(directory_pieces, directory_pieces_size, &index, &return_info, &type, NAV_READ))
			{
				// Caso o que foi encontrado esteja no root_dir.
				if (return_info == ROOT_DIR)
				{
					// Se o atributo da entrada de diretório encontrada não for 0x1.
					if (type != SUB_DIR)
						fprintf(stderr, "Não é um diretório.\n");
					else
					{
						// Percorre o diretório procurando entradas de diretório referenciadas.
						bool found_anything = false;
						for (int i = 0; i < 32; i++)
						{
							if (root_dir[i].first_block != 0x00)
							{
								found_anything = true;
								fprintf(stdout, "%s\n", root_dir[i].filename);
							}
						}

						// Caso nenhuma entrada de diretório ocupada seja encontrada no diretório.
						if (!found_anything)
								fprintf(stderr, "Diretório vazio.\n");
					}
				}
				// Caso o que foi encontrado esteja nos clusteres de dados.
				else if (return_info == DATA_DIR)
				{
					// Se o atributo da entrada de diretório encontrada não for 0x1.
					if (type != SUB_DIR)
						fprintf(stderr, "Não é um diretório.\n");
					else
					{
						// Percorre o diretório procurando entradas de diretório referenciadas.
						bool found_anything = false;
						for (int i = 0; i < 32; i++)
						{
							if (get_data_cluster(index).dir[i].first_block != 0x00)
							{
								found_anything = true;
								fprintf(stdout, "%s\n", get_data_cluster(index).dir[i].filename);
							}
						}

						// Caso nenhuma entrada de diretório ocupada seja encontrada no diretório.
						if (!found_anything)
							fprintf(stderr, "Diretório vazio.\n");
					}
				}
			}
			else
			{
				// Caso a operação falhe, mostra o erro correspondente.
				switch (return_info)
				{
					case INVALID_DIR:
						fprintf(stderr, "Diretório inválido.\n");
						break;
					case NOT_FOUND_DIR:
						fprintf(stderr, "Diretório inexistente.\n");
						break;
					case NOT_A_DIR:
						fprintf(stderr, "Não é um diretório.\n");
						break;
					default:
						fprintf(stderr, "Não faço a mínima ideia do que deu errado.\n");
				}
			}
		}
		else
			fprintf(stderr, "Número de argumentos inválidos para o comando ls.\n");

		save();

	}
	else if (strcmp(command_pieces[0], "mkdir") == 0)
	{
		if (command_pieces_size == 2)
		{
			unsigned index = 0, return_info = 0, type = 0;
			// Chama directory_navigator com NAV_CREATE, ou seja, caso não encontre um diretório no decorrer do diretório passado, cria-o
			if (!directory_navigator(directory_pieces, directory_pieces_size, &index, &return_info, &type, NAV_CREATE))
			{
				// Caso a operação falhe, mostra o erro correspondente.
				switch (return_info)
				{
					case INVALID_DIR:
						fprintf(stderr, "Diretório inválido.\n");
						break;
					case FULL_DIR:
						fprintf(stderr, "Diretório lotado.\n");
						break;
					default:
						fprintf(stderr, "Não foi possível criar o diretório. (%d)\n", return_info);
				}

			}
		}
		else
			fprintf(stderr, "Número de argumentos inválidos para o comando mkdir.\n");

		save();

	}
	else if (strcmp(command_pieces[0], "create") == 0)
	{
		if (command_pieces_size == 2)
		{
			unsigned index = 0, return_info = 0, type = 0;
			// Excluindo a ultima parte passada no diretório (directory_pieces_size - 2), cria os diretórios não existente no decorrer do diretório passado (NAV_CREATE).
			if (directory_navigator(directory_pieces, directory_pieces_size - 2, &index, &return_info, &type, NAV_CREATE))
			{
				return_info = 0;
				// Cria o arquivo a partir do diretório passado (garantia de existência pela função passada).
				if (!create_file(directory_pieces, directory_pieces_size, &return_info, index))
				{
					// Caso a operação (criar arquivo) falhe, mostra o erro correspondente.
					switch (return_info)
					{
						case FULL_DIR:
							fprintf(stderr, "Diretório lotado.\n");
							break;
						default:
							fprintf(stderr, "Não foi possível criar o arquivo. (%d)\n", return_info);
					}
				}
			}
			else
			{
				// Caso a operação (caminhar até o diretório onde o arquivo será criado) falhe, mostra o erro correspondente.
				switch (return_info)
				{
					case INVALID_DIR:
						fprintf(stderr, "Diretório inválido.\n");
						break;
					case FULL_DIR:
						fprintf(stderr, "Diretório lotado.\n");
						break;
					default:
						fprintf(stderr, "Não foi possível criar o diretório. (%d)\n", return_info);
				}
			}
		}
		else
			fprintf(stderr, "Número de argumentos inválidos para o comando create.\n");

		save();
	}
	else if (strcmp(command_pieces[0], "unlink") == 0)
	{
		if (command_pieces_size == 2)
		{
			unsigned index = 0, return_info = 0, type = 0;
			// Navega nos diretórios apagando a ultima parcela do mesmo, seja ela um arquivo ou diretório (NAV_DELETE).
			if (!directory_navigator(directory_pieces, directory_pieces_size, &index, &return_info, &type, NAV_DELETE))
			{
				// Caso a operação falhe, mostra o erro correspondente.
				switch (return_info)
				{
					case INVALID_DIR:
						fprintf(stderr, "Diretório inválido.\n");
						break;
					case NOT_EMPTY_DIR:
						fprintf(stderr, "Somente diretórios vazios podem ser deletados.\n");
						break;
					default:
						fprintf(stderr, "Não foi possível deletar o diretório/arquivo. (%d)\n", return_info);
				}
			}
		}
		else
			fprintf(stderr, "Número de argumentos inválidos para o comando unlink.\n");

		save();
	}
	else if (strcmp(command_pieces[0], "write") == 0)
	{
		if (command_pieces_size > 2)
		{
			unsigned index = 0, return_info = 0, type = 0;
			// Caminha até o diretório onde o arquivo em que será escrito está, caso alguma entrada de diretório não seja encontrada, dará erro (NAV_READ).
			if (directory_navigator(directory_pieces, directory_pieces_size - 2, &index, &return_info, &type, NAV_READ))
			{
				return_info = 0;
				// Escreve no arquivo passado, uma vez que o caminho até ele está correto.
				if (!write_file(directory_pieces, directory_pieces_size, index, input_string, &return_info))
				{
					// Caso a operação (escrita no arquivo) falhe, mostra o erro correspondente.
					switch (return_info)
					{
						case NOT_A_FILE:
							fprintf(stderr, "Não é um arquivo.\n");
							break;
						case NOT_FOUND_FILE:
							fprintf(stderr, "Arquivo não encontrado.\n");
							break;
						default:
							fprintf(stderr, "Não foi possível escrever no arquivo. (%d)\n", return_info);
					}
				}
			}
			else
			{
				// Caso a operação (caminhar até o arquivo em que será escrito) falhe, mostra o erro correspondente.
				switch (return_info)
				{
					case INVALID_DIR:
						fprintf(stderr, "Diretório inválido.\n");
						break;
					case NOT_FOUND_DIR:
						fprintf(stderr, "Diretório inexistente.\n");
						break;
					case NOT_A_DIR:
						fprintf(stderr, "Não é um diretório.\n");
						break;
					default:
						fprintf(stderr, "Não foi possível navegar até o arquivo. (%d)\n", return_info);
				}
			}
		}
		else
			fprintf(stderr, "Número de argumentos inválido para o comando write.\n");

		save();
	}
	else if (strcmp(command_pieces[0], "append") == 0)
	{
		if (command_pieces_size > 2)
		{
			unsigned index = 0, return_info = 0, type = 0;
			// Caminha até o diretório onde o arquivo em que será acrescido conteúdo está, caso alguma entrada de diretório não seja encontrada, dará erro (NAV_READ).
			if (directory_navigator(directory_pieces, directory_pieces_size - 2, &index, &return_info, &type, NAV_READ))
			{
				return_info = 0;
				// Acrescenta o conteúdo no arquivo, uma vez que o caminho até o mesmo está correto.
				if (!append_file(directory_pieces, directory_pieces_size, index, input_string, &return_info))
				{
					// Caso a operação (acrescer dados no arquivo) falhe, mostra o erro correspondente.
					switch (return_info)
					{
						case NOT_A_FILE:
							fprintf(stderr, "Não é um arquivo.\n");
							break;
						case NOT_FOUND_FILE:
							fprintf(stderr, "Arquivo não encontrado.\n");
							break;
						default:
							fprintf(stderr, "Não foi possível acrescentar no arquivo. (%d)\n", return_info);
					}
				}
			}
			else
			{
				// Caso a operação (caminhar até o arquivo em que será acrescido dados) falhe, mostra o erro correspondente.
				switch (return_info)
				{
					case INVALID_DIR:
						fprintf(stderr, "Diretório inválido.\n");
						break;
					case NOT_FOUND_DIR:
						fprintf(stderr, "Diretório inexistente.\n");
						break;
					case NOT_A_DIR:
						fprintf(stderr, "Não é um diretório.\n");
						break;
					default:
						fprintf(stderr, "Não foi possível navegar até o arquivo. (%d)\n");
				}
			}
		}
		else
			fprintf(stderr, "Número de argumentos inválido para o comando append.\n");

		save();
	}
	else if (strcmp(command_pieces[0], "read") == 0)
	{
		if (command_pieces_size == 2)
		{
			unsigned index = 0, return_info = 0, type = 0;

			// Caminha até o diretório onde o arquivo que será lido está, caso alguma entrada de diretório não seja encontrada, dará erro (NAV_READ).
			if (directory_navigator(directory_pieces, directory_pieces_size - 2, &index, &return_info, &type, NAV_READ))
			{
				return_info = 0;
				char* read_data = NULL;
				// Lê o arquivo, uma vez que o caminho até ele esteja correto.
				if (read_file(directory_pieces, directory_pieces_size, index, &read_data, &return_info))
				{
					fprintf(stdout, "%s\n", read_data);
					free(read_data);
				}
				else
				{
					// Caso a operação (ler arquivo) falhe, mostra o erro correspondente.
					switch (return_info)
					{
						case NOT_A_FILE:
							fprintf(stderr, "Não é um arquivo.\n");
							break;
						case NOT_FOUND_FILE:
							fprintf(stderr, "Arquivo não encontrado.\n");
							break;
						default:
							fprintf(stderr, "Não foi possível ler o arquivo. (%d)\n", return_info);
					}
				}
			}
			else
			{
				// Caso a operação (caminhar até o arquivo que será lido) falhe, mostra o erro correspondente.
				switch (return_info)
				{
					case INVALID_DIR:
						fprintf(stderr, "Diretório inválido.\n");
						break;
					case NOT_FOUND_DIR:
						fprintf(stderr, "Diretório inexistente.\n");
						break;
					case NOT_A_DIR:
						fprintf(stderr, "Não é um diretório.\n");
						break;
					default:
						fprintf(stderr, "Não foi possível navegar até o arquivo. (%d)\n", return_info);
				}
			}
		}
		else
			fprintf(stderr, "Número de argumentos inválidos para o comando read.\n");

		save();
	}
	else if (strcmp(command_pieces[0], "exit") == 0)
		end_shell = true;
	else
		fprintf(stderr, "Comando inexistente.\n");

	free_structure(&command_pieces, command_pieces_size);
	free_structure(&directory_pieces, directory_pieces_size);
	free(input_string);

	if (end_shell)
		exit(EXIT_SUCCESS);
}

// Separa o comando por ' '.
void explode_command(char* command, char*** command_pieces, unsigned* command_pieces_size)
{
	unsigned piece_counter = 0;
	char* token = strtok(command, " \t"); // Faz o slice inicial.

	while (token != NULL)
	{
		if (strcmp(token, "\n") != 0)
		{
			(*command_pieces) = (char**) realloc((*command_pieces), (piece_counter + 1) * sizeof(char*)); // Cria mais um espaço.
			(*command_pieces)[piece_counter] = (char*) malloc((strlen(token) + 1) * sizeof(char)); // Aloca mais uma string.
			strcpy((*command_pieces)[piece_counter], token); // Copia o token.
			piece_counter++; // Incrementa o contador.
		}

		token = strtok(NULL, " \t"); // Faz 'slice'
	}

	// Caso haja '\n' no final do comando, substitui por '\0'.
	if (piece_counter > 0)
		if ((*command_pieces)[piece_counter - 1][strlen((*command_pieces)[piece_counter - 1]) - 1] == '\n')
			(*command_pieces)[piece_counter - 1][strlen((*command_pieces)[piece_counter - 1]) - 1] = '\0';

	*command_pieces_size = piece_counter;
}

// Separa o diretório passado por '/'.
void explode_directory(char* directory, char*** directory_pieces, unsigned* directory_pieces_size)
{
	unsigned piece_counter = 0;

	// Caso só haja '/' no diretório passado, cria um vetor de uma posição com o '/' e retorna.
	if (strlen(directory) == 1)
	{
		(*directory_pieces) = (char**) malloc(1 * sizeof(char*));
		(*directory_pieces)[piece_counter] = (char*) malloc(2 * sizeof(char));
		strcpy((*directory_pieces)[piece_counter], "/");
		*directory_pieces_size = ++piece_counter;
		return;
	}

	// Faz o slice inicial.
	char* token = strtok(directory, "/");

	while (token != NULL)
	{
		if (strcmp(token, "\n") != 0)
		{
			// A cada 'parte' do diretório, aloca um espaoço para o '/' e outro para o nome da entrada de diretório.
			(*directory_pieces) = (char**) realloc((*directory_pieces), (piece_counter + 2) * sizeof(char*));
			(*directory_pieces)[piece_counter] = (char*) malloc(2 * sizeof(char)); // Aloca espaço para o '/' e para o '\0'.
			(*directory_pieces)[piece_counter + 1] = (char*) malloc((strlen(token) + 1) * sizeof(char*)); // Aloca espaço para o nome da entrada de diretório e para o '\0'.

			strcpy((*directory_pieces)[piece_counter], "/"); // Copia a '/'.
			strcpy((*directory_pieces)[piece_counter + 1], token); // Copia o nome da entrada de diretório.

			piece_counter = piece_counter + 2;
		}

		token = strtok(NULL, "/");
	}

	// Caso o último caractere seja '\n', substitui por '\0'.
	if (piece_counter > 0)
		if ((*directory_pieces)[piece_counter - 1][strlen((*directory_pieces)[piece_counter - 1]) - 1] == '\n')
			(*directory_pieces)[piece_counter - 1][strlen((*directory_pieces)[piece_counter - 1]) - 1] = '\0';

	*directory_pieces_size = piece_counter;
}

void get_input_string(char* command, char** string_input)
{
	unsigned indicator_found = 0, input_size = 0;

	// Percorre o comando passado pelo usuário procurando aspas duplas '"'.
	for (int i = 0; i < strlen(command); i++)
	{
		if (command[i] == '"')
		{
			indicator_found++;
			continue;
		}

		// Tudo entre duas aspas duplas é considerado como dado de entrada, logo posto dentro de um vetor.
		if (indicator_found == 1)
		{
			(*string_input) = (char*) realloc((*string_input), (input_size + 1) * sizeof(char));
			(*string_input)[input_size] = command[i];
			input_size++;
		}

		// O código para na segunda aspa dupla encontrada.
		if (indicator_found > 1)
			break;
	}

	// Aloca mais um espaço para o '\0', para a string poder ser usada no strlen, uma vez que o sistema de arquivos inteiro é preenchido com 0x00.
	(*string_input) = (char*) realloc((*string_input), (input_size + 1) * sizeof(char));
	(*string_input)[input_size] = '\0';
}

bool validate_directory(char** directory_pieces, unsigned directory_pieces_size)
{
	// Checa em intervalo de intermitência (1:2) se possui '/' nos lugares corretos.
	for (int i = 0; i < directory_pieces_size; i = i + 2)
		if (i >= directory_pieces_size)
			return false;
		else
			if (strcmp(directory_pieces[i], "/") != 0)
				return false;

	return true;
}

bool directory_navigator(char** directory_pieces, unsigned directory_pieces_size, unsigned* index, unsigned* return_info, unsigned* type, unsigned nav_type)
{
	unsigned short next_block = 0x00;

	if (directory_pieces_size == 1)
	{
		*index = next_block;
		*return_info = ROOT_DIR;
		*type = SUB_DIR;
		return true;
	}

	for (int i = 1; i < directory_pieces_size; i = i + 2)
	{
		if (i == 1)
		{
			bool find_dir = false;
			// Percorre os diretórios do root_dir.
			for (int j = 0; j < 32; j++)
			{
				if (strcmp(root_dir[j].filename, directory_pieces[1]) == 0)
				{
					if (root_dir[j].attributes == 0x1)
					{
						find_dir = true;
						next_block = root_dir[j].first_block;

						// Caso o diretório seja encontrado, e o comando delete seja passado, apaga a entrada de diretório.
						if (nav_type == NAV_DELETE && directory_pieces_size == 2)
						{
							// Checa para ver se o diretório está vazio.
							for (int k = 0; k < 32; k++)
							{
								if (get_data_cluster(next_block).dir[k].first_block != 0x00)
								{
									*return_info = NOT_EMPTY_DIR;
									return false;
								}
							}

							// Reseta os valores da entrada de diretório.
							root_dir[j].first_block = 0x00;
							root_dir[j].attributes = 0x0;
							fat[root_dir[j].first_block] = 0x00;
							memset(root_dir[j].filename, 0x00, 18);
							return true;
						}

						// Diretório encontrado.
						if (directory_pieces_size == 2)
						{
							*index = next_block;
							*return_info = DATA_DIR;
							*type = SUB_DIR;
							return true;
						}

						break;
					}
					else
					{
						if (directory_pieces_size == 2)
						{
							if (nav_type == NAV_DELETE)
							{
								unsigned local_next_block = root_dir[j].first_block;
								unsigned* file_trace_back = NULL;

								int iteration = 0;
								// Monta um vetor para guardar todos blocos ocupados pelo arquivo.
								do
								{
									if (iteration != 0)
										local_next_block = fat[local_next_block];

									if (local_next_block != 0xffff)
									{
										file_trace_back = (unsigned*) realloc(file_trace_back, (iteration + 1) * sizeof(unsigned));
										file_trace_back[iteration] = local_next_block;
										iteration++;
									}
								}
								while (local_next_block != 0xffff);

								// Percorre o vetor resetando os valores da fat e dos clusters de dados.
								for (int k = 0; k < iteration; k++)
								{
									fat[file_trace_back[k]] = 0x00;
									data_cluster cluster;
									memset(cluster.data, 0x00, CLUSTER_SIZE);
									save_data_cluster(file_trace_back[k], cluster);
								}

								free(file_trace_back);

								// Reseta os valores da entrada de diretório.
								root_dir[j].first_block = 0x00;
								root_dir[j].attributes = 0x0;
								root_dir[j].size = 0x00;
								memset(root_dir[j].filename, 0x00, 18);

								return true;
							}

							// Arquivo encontrado.
							*index = next_block;
							*return_info = DATA_DIR;
							*type = FILE_DIR;
							return true;
						}
						else
						{
							// Não é um diretório.
							*return_info = NOT_A_DIR;
							return false;
						}
					}
				}
			}

			// Diretório não encontrado.
			if (!find_dir)
			{
				// Diretório não encontrado para ser lido ou deletado.
				if (nav_type == NAV_READ || nav_type == NAV_DELETE)
				{
					*return_info = NOT_FOUND_DIR;
					return false;
				}
				// Cria o diretório, caso necessário para os comandos create e mkdir.
				else if (nav_type == NAV_CREATE)
				{
					bool full_dir = true;

					for (int j = 0; j < 32; j++)
					{
						if (root_dir[j].first_block == 0x00)
						{
							root_dir[j].first_block = get_available_cluster();
							// Sistema de arquivos cheio, não há espaço disponível.
							if (root_dir[j].first_block == 0x00)
							{
								*return_info = BLOATED_SYSTEM;
								return false;
							}
							// Cria a entrada de diretório.
							root_dir[j].attributes = 0x1;
							strcpy(root_dir[j].filename, directory_pieces[1]);
							fat[root_dir[j].first_block] = 0xffff;
							next_block = root_dir[j].first_block;
							full_dir = false;
							*index = root_dir[j].first_block;
							break;
						}
					}

					// Diretório lotado.
					if (full_dir)
					{
						*return_info = FULL_DIR;
						return false;
					}
				}
			}
		}
		// Lida com diretórios/arquivos fora do root_dir.
		else
		{
			bool find_dir = false;
			for (int j = 0; j < 32; j++)
			{
				if (strcmp(get_data_cluster(next_block).dir[j].filename, directory_pieces[i]) == 0)
				{
					if (get_data_cluster(next_block).dir[j].attributes == 0x1)
					{
						find_dir = true;

						// Caso o diretório seja encontrado, e o comando delete seja passado, apaga o diretório.
						if (nav_type == NAV_DELETE && (directory_pieces_size - 1) == i)
						{
							// Checa para ver se o diretório está vazio.
							for (int k = 0; k < 32; k++)
							{
								if (get_data_cluster(get_data_cluster(next_block).dir[j].first_block).dir[k].first_block != 0x00)
								{
									*return_info = NOT_EMPTY_DIR;
									return false;
								}
							}

							// Reseta os valores da entrada de diretório.
							data_cluster cluster;
							memset(cluster.dir, 0x00, CLUSTER_SIZE);
							cluster = get_data_cluster(next_block);
							cluster.dir[j].first_block = 0x00;
							cluster.dir[j].attributes = 0x0;
							fat[cluster.dir[j].first_block] = 0x00;
							memset(cluster.dir[j].filename, 0x00, 18);
							save_data_cluster(next_block, cluster);
							return true;
						}

						// Atualiza o próximo bloco a ser visto.
						next_block = get_data_cluster(next_block).dir[j].first_block;

						// Caso seja a última 'peça' do diretório, retorna as informações e o 'next_block'.
						if ((directory_pieces_size - 1) == i)
						{
							*index = next_block;
							*return_info = DATA_DIR;
							*type = SUB_DIR;
							return true;
						}

						break;
					}
					else
					{
						if ((directory_pieces_size - 1) == i)
						{
							if (nav_type == NAV_DELETE)
							{
								unsigned local_next_block = get_data_cluster(next_block).dir[j].first_block;
								unsigned* file_trace_back = NULL;

								int iteration = 0;
								// Monta um vetor para guardar todos blocos ocupados pelo arquivo.
								do
								{
									if (iteration != 0)
										local_next_block = fat[local_next_block];

									if (local_next_block != 0xffff)
									{
										file_trace_back = (unsigned*) realloc(file_trace_back, (iteration + 1) * sizeof(unsigned));
										file_trace_back[iteration] = local_next_block;
										iteration++;
									}
								}
								while (local_next_block != 0xffff);

								// Percorre o vetor resetando os valores da fat e dos clusters de dados.
								for (int k = 0; k < iteration; k++)
								{
									fat[file_trace_back[k]] = 0x00;
									data_cluster cluster;
									memset(cluster.data, 0x00, CLUSTER_SIZE);
									save_data_cluster(file_trace_back[k], cluster);
								}

								free(file_trace_back);

								// Reseta os valores da entrada de diretório.
								data_cluster cluster;
								memset(cluster.dir, 0x00, CLUSTER_SIZE);
								cluster = get_data_cluster(next_block);
								cluster.dir[j].first_block = 0x00;
								cluster.dir[j].attributes = 0x0;
								cluster.dir[j].size = 0x00;
								memset(cluster.dir[j].filename, 0x00, 18);
								save_data_cluster(next_block, cluster);

								return true;
							}

							// Arquivo encontrado.
							*index = next_block;
							*return_info = DATA_DIR;
							*type = FILE_DIR;
							return true;
						}
						else
						{
							// Não é um diretório.
							*return_info = NOT_A_DIR;
							return false;
						}
					}
				}
			}

			// Diretório não encontrado.
			if (!find_dir)
			{
				// Diretório a ser lido ou deletado não encontrado.
				if (nav_type == NAV_READ || nav_type == NAV_DELETE)
				{
					*return_info = NOT_FOUND_DIR;
					return false;
				}
				// Diretório não encontrado, cria-o, dependendo da necessidade.
				else if (nav_type == NAV_CREATE)
				{
					bool full_dir = true;
					for (int j = 0; j < 32; j++)
					{
						// Cria um novo diretório, caso necessário.
						if (get_data_cluster(next_block).dir[j].first_block == 0x00)
						{
							data_cluster cluster;
							memset(cluster.dir, 0x00, CLUSTER_SIZE);
							cluster = get_data_cluster(next_block);
							cluster.dir[j].first_block = get_available_cluster();
							// Sistema de arquivos cheio, não há espaço disponível.
							if (cluster.dir[j].first_block == 0x00)
							{
								*return_info = BLOATED_SYSTEM;
								return false;
							}
							// Cria entrada de diretório.
							cluster.dir[j].attributes = 0x1;
							strcpy(cluster.dir[j].filename, directory_pieces[i]);
							save_data_cluster(next_block, cluster);
							fat[get_data_cluster(next_block).dir[j].first_block] = 0xffff;
							next_block = get_data_cluster(next_block).dir[j].first_block;
							full_dir = false;
							*index = next_block;
							break;
						}
					}

					// Diretório lotado.
					if (full_dir)
					{
						*return_info = FULL_DIR;
						return false;
					}
				}
			}
		}
	}

	if (nav_type == NAV_CREATE)
		return true;
}

bool create_file(char** directory_pieces, unsigned directory_pieces_size, unsigned* return_info, unsigned index)
{
	unsigned next_block = index;

	// Arquivo a ser criado no root_dir.
	if (next_block == 0x00)
	{
		bool full_dir = true;
		// Percorre os diretórios do root_dir a procura de uma entrada de diretório livre.
		for (int i = 0; i < 32; i++)
		{
			if (root_dir[i].first_block == 0x00)
			{
				full_dir = false;
				root_dir[i].first_block = get_available_cluster();
				// Sistema de arquivos cheio, não há espaço disponível.
				if (root_dir[i].first_block == 0x00)
				{
					*return_info = BLOATED_SYSTEM;
					return false;
				}
				// Cria a entrada de diretório para o arquivo.
				root_dir[i].attributes = 0x0;
				fat[root_dir[i].first_block] = 0xffff;
				strcpy(root_dir[i].filename, directory_pieces[directory_pieces_size - 1]);

				return true;
			}
		}

		// Diretório cheio.
		if (full_dir)
		{
			*return_info = FULL_DIR;
			return false;
		}
	}
	// Arquivo a ser criado nos clusteres de dados.
	else
	{
		bool full_dir = true;
		// Percorre os diretórios do cluster a procura de uma entrada de diretório vazia.
		for (int i = 0; i < 32; i++)
		{
			if (get_data_cluster(next_block).dir[i].first_block == 0x00)
			{
				full_dir = false;
				data_cluster cluster;
				memset(cluster.dir, 0x00, CLUSTER_SIZE);
				cluster = get_data_cluster(next_block);
				cluster.dir[i].first_block = get_available_cluster();
				// Sistema de arquivos cheio, não há espaço disponível.
				if (cluster.dir[i].first_block == 0x00)
				{
					*return_info = BLOATED_SYSTEM;
					return false;
				}
				// Cria a entrada de diretório para o arquivo.
				cluster.dir[i].attributes = 0x0;
				fat[cluster.dir[i].first_block] = 0xffff;
				strcpy(cluster.dir[i].filename, directory_pieces[directory_pieces_size - 1]);
				save_data_cluster(next_block, cluster);

				return true;
			}
		}

		// Diretório lotado.
		if (full_dir)
		{
			*return_info = FULL_DIR;
			return false;
		}
	}
}

bool write_file(char** directory_pieces, unsigned directory_pieces_size, unsigned index, char* data, unsigned* return_info)
{
	unsigned next_block = index;
	unsigned dir_entry_block = 0x00;
	unsigned dir_entry_index = 0x00;
	bool find_file = false;

	if (next_block == 0x00)
	{
		// Percorre os diretórios de root_dir a procura do arquivo.
		for (int i = 0; i < 32; i++)
		{
			if (strcmp(root_dir[i].filename, directory_pieces[directory_pieces_size - 1]) == 0)
			{
				// A entrada de diretório encontrada não é um arquivo.
				if (root_dir[i].attributes == 0x1)
				{
					*return_info = NOT_A_FILE;
					return false;
				}

				find_file = true;
				next_block = root_dir[i].first_block;
				dir_entry_index = i;
				break;
			}
		}
	}
	else
	{
		// Percorre os diretórios dos clusteres de dados a procura do arquivo.
		for (int i = 0; i < 32; i++)
		{
			if (strcmp(get_data_cluster(next_block).dir[i].filename, directory_pieces[directory_pieces_size - 1]) == 0)
			{
				// A entrada de diretório encontrada não é um arquivo.
				if (get_data_cluster(next_block).dir[i].attributes == 0x1)
				{
					*return_info = NOT_A_FILE;
					return false;
				}

				find_file = true;
				next_block = get_data_cluster(next_block).dir[i].first_block;
				dir_entry_block = next_block;
				dir_entry_index = i;
				break;
			}
		}
	}

	// Arquivo não encontrado.
	if (!find_file)
	{
		*return_info = NOT_FOUND_FILE;
		return false;
	}

	int data_size = strlen(data);
	int ceiling = 0;
	int multiplier = 0;

	// A cada iteração escreve em um cluster.
	do
	{
		// Na primeira iteração naõ é necessário criar um bloco novo.
		if (multiplier != 0)
		{
			// Caso seja necessário, aloca um cluster novo para o arquivo.
			if (fat[next_block] == 0xffff)
			{
				fat[next_block] = get_available_cluster();
				// Sistema de arquivos cheio, não há espaço disponível.
				if (fat[next_block] == 0x00)
				{
					*return_info = BLOATED_SYSTEM;
					return false;
				}
			}

			next_block = fat[next_block];
			fat[next_block] = 0xffff;
		}

		// Define o teto, para não escrever onde não se deve.
		if (data_size >= CLUSTER_SIZE)
			ceiling = CLUSTER_SIZE;
		else
			ceiling = data_size;

		// Até o teto estipulado, escreve os dados.
		for (int i = 0; i < ceiling; i++)
		{
			data_cluster cluster;
			memset(cluster.data, 0x00, CLUSTER_SIZE);
			cluster = get_data_cluster(next_block);
			cluster.data[i] = data[(multiplier * CLUSTER_SIZE) + i];
			save_data_cluster(next_block, cluster);
		}

		// Incrementa o tamanho do arquivo.
		if (dir_entry_block == 0x00)
			root_dir[dir_entry_index].size += CLUSTER_SIZE;
		else
		{
			data_cluster cluster;
			memset(cluster.dir, 0x00, CLUSTER_SIZE);
			cluster = get_data_cluster(dir_entry_block);
			cluster.dir[dir_entry_index].size += CLUSTER_SIZE;
			save_data_cluster(dir_entry_block, cluster);
		}

		data_size = data_size - ceiling; // Remove o teto do tamanho do dado a ser escrito para a próxima iteração.
		multiplier++;
	} while (data_size != 0);

	return true;
}

bool append_file (char** directory_pieces, unsigned directory_pieces_size, unsigned index, char* data, unsigned* return_info)
{
	unsigned next_block = index;
	unsigned dir_entry_block = 0x00;
	unsigned dir_entry_index = 0x00;
	bool find_file = false;

	if (next_block == 0x00)
	{
		// Percorre os diretórios de root_dir a procura do arquivo.
		for (int i = 0; i < 32; i++)
		{
			if (strcmp(root_dir[i].filename, directory_pieces[directory_pieces_size - 1]) == 0)
			{
				// A entrada de diretório encontrada não é um arquivo.
				if (root_dir[i].attributes == 0x1)
				{
					*return_info = NOT_A_FILE;
					return false;
				}

				find_file = true;
				next_block = root_dir[i].first_block;
				dir_entry_index = i;
				break;
			}
		}
	}
	else
	{
		// Percorre os diretórios dos clusteres de dados a procura do arquivo.
		for (int i = 0; i < 32; i++)
		{
			if (strcmp(get_data_cluster(next_block).dir[i].filename, directory_pieces[directory_pieces_size - 1]) == 0)
			{
				// A entrada de diretório encontrada não é um arquivo.
				if (get_data_cluster(next_block).dir[i].attributes == 0x1)
				{
					*return_info = NOT_A_FILE;
					return false;
				}

				find_file = true;
				next_block = get_data_cluster(next_block).dir[i].first_block;
				dir_entry_block = next_block;
				dir_entry_index = i;
				break;
			}
		}
	}

	// Arquivo não encontrado.
	if (!find_file)
	{
		*return_info = NOT_FOUND_FILE;
		return false;
	}

	// Encontra o primeiro espaço vazio no arquivo.
	int iteration = 0;
	int empty_index = 0;
	do
	{
		if (iteration != 0)
			next_block = fat[next_block];

		// Percorre o cluster procurando espaço vazio.
		for (int i = 0; i < CLUSTER_SIZE; i++)
		{
			if (get_data_cluster(next_block).data[i] == 0x00)
			{
				empty_index = i;
				break;
			}
		}

		iteration++;
	} while (fat[next_block] != 0xffff);

	// Insere os dados partindo do espaço vazio encontrado anteriormente.
	int data_size = strlen(data);
	int ceiling = 0;
	int multiplier = 0;
	do
	{
		// Na primeira iteração não é necessário criar um bloco novo.
		if (multiplier != 0)
		{
			// Aloca um novo cluster para o arquivo, caso necessário.
			if (fat[next_block] == 0xffff)
			{
				fat[next_block] = get_available_cluster();
				// Sistema de arquivos cheio, não há espaço disponível.
				if (fat[next_block] == 0x00)
				{
					*return_info = BLOATED_SYSTEM;
					return false;
				}
			}

			next_block = fat[next_block];
			fat[next_block] = 0xffff;
		}

		// Define o teto, para não escrever onde não se deve.
		if ((data_size + empty_index) >= CLUSTER_SIZE)
			ceiling = CLUSTER_SIZE;
		else
			ceiling = data_size + empty_index;

		// Escreve os dados até o teto estipulado.
		for (int i = empty_index; i < ceiling; i++)
		{
			data_cluster cluster;
			memset(cluster.data, 0x00, CLUSTER_SIZE);
			cluster = get_data_cluster(next_block);
			cluster.data[i] = data[(multiplier * CLUSTER_SIZE) + (i - empty_index)];
			save_data_cluster(next_block, cluster);
		}

		// Incrementa o tamanho do arquivo.
		if (dir_entry_block == 0x00)
			root_dir[dir_entry_index].size += CLUSTER_SIZE;
		else
		{
			data_cluster cluster;
			memset(cluster.dir, 0x00, CLUSTER_SIZE);
			cluster = get_data_cluster(dir_entry_block);
			cluster.dir[dir_entry_index].size += CLUSTER_SIZE;
			save_data_cluster(dir_entry_block, cluster);
		}

		data_size = data_size - ceiling;
		empty_index = 0;
		multiplier++;
	} while (data_size != 0);

	return true;
}

bool read_file (char** directory_pieces, unsigned directory_pieces_size, unsigned index, char** data, unsigned* return_info)
{
	unsigned next_block = index;
	unsigned dir_entry_block = 0x00;
	unsigned dir_entry_index = 0x00;
	bool find_file = false;

	// Caso a entrada de diretório do arquivo solicitado esteja no root_dir.
	if (next_block == 0x00)
	{
		// Percorre os diretórios a procura da entrada de diretório do arquivo.
		for (int i = 0; i < 32; i++)
		{
			if (strcmp(root_dir[i].filename, directory_pieces[directory_pieces_size - 1]) == 0)
			{
				// Checa se a entrada de diretório encontrada corresponde a um arquivo.
				if (root_dir[i].attributes == 0x1)
				{
					*return_info = NOT_A_FILE;
					return false;
				}

				find_file = true;
				next_block = root_dir[i].first_block;
				dir_entry_index = i;
				break;
			}
		}
	}
	// Caso a entrada de diretório do arquivo esteja nos clusteres de dados.
	else
	{
		// Percorre os diretórios a procura da entrada de diretório do arquivo.
		for (int i = 0; i < 32; i++)
		{
			// Se o nome passado der match.
			if (strcmp(get_data_cluster(next_block).dir[i].filename, directory_pieces[directory_pieces_size - 1]) == 0)
			{
				// Checa se a entrada de diretório encontrada corresponde a um arquivo.
				if (get_data_cluster(next_block).dir[i].attributes == 0x1)
				{
					*return_info = NOT_A_FILE;
					return false;
				}

				find_file = true;
				next_block = get_data_cluster(next_block).dir[i].first_block;
				dir_entry_block = next_block;
				dir_entry_index = i;
				break;
			}
		}
	}

	// Arquivo solicitado não encontrado.
	if (!find_file)
	{
		*return_info = NOT_FOUND_FILE;
		return false;
	}

	int iteration = 0;
	int multiplier = 1;
	int data_iterator = 0;
	// Percore os clusters de dados do arquivo colocando byte a byte em uma string.
	do
	{
		if (iteration != 0)
			next_block = fat[next_block];

		(*data) = (char*) realloc((*data), multiplier * CLUSTER_SIZE * sizeof(char));
		for (int i = 0; i < CLUSTER_SIZE; i++)
		{
			if (get_data_cluster(next_block).data[i] != 0x00)
				(*data)[data_iterator++] = get_data_cluster(next_block).data[i];
			else
				break;
		}
		iteration++;
		multiplier++;
	} while (fat[next_block] != 0xffff);

	// Cria mais um espaço para o '\0' caso necessário.
	if (data_iterator == CLUSTER_SIZE)
		(*data) = (char*) realloc((*data), (data_iterator + 1) * sizeof(char));

	// Adiciona o '\0' ao final da string lida.
	(*data)[data_iterator] = '\0';

	return true;
}

unsigned get_available_cluster()
{
	// Percorre a fat a procura de um índice livre.
	for (int i = 10; i < NUM_CLUSTER; i++)
	{
		if (fat[i] == 0x00)
			return i;
	}

	// Caso não encontre, retorna um índice inválido.
	return 0x00;
}

void init(void)
{
	FILE* ptr_file;
	int i;
	ptr_file = fopen(fat_name,"wb");
	if (ptr_file == NULL)
	{
		fprintf(stderr, "Não foi possível abrir o arquivo %s.\n", fat_name);
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < 2; ++i)
		boot_block[i] = 0xbb;

	fwrite(&boot_block, sizeof(boot_block), 1,ptr_file);

	fat[0] = 0xfffd;
	for (i = 1; i < 9; ++i)
		fat[i] = 0xfffe;

	fat[9] = 0xffff;
	for (i = 10; i < NUM_CLUSTER; ++i)
		fat[i] = 0x0000;

	fwrite(&fat, sizeof(fat), 1, ptr_file);
	fwrite(&root_dir, sizeof(root_dir), 1,ptr_file);

	for (i = 0; i < 4086; ++i)
		fwrite(&clusters, sizeof(data_cluster), 1, ptr_file);

	fclose(ptr_file);
}

void load()
{
	FILE* ptr_file;
	int i;
	ptr_file = fopen(fat_name, "rb");
	if (ptr_file == NULL)
	{
		fprintf(stderr, "Não foi possível abrir o arquivo %s.\n", fat_name);
		exit(EXIT_FAILURE);
	}
	fseek(ptr_file, sizeof(boot_block), SEEK_SET); // Chega até a FAT.
	fread(fat, sizeof(fat), 1, ptr_file); // Lê a FAT.
	fread(root_dir, sizeof(root_dir), 1, ptr_file); // Lê o root_dir.
	fclose(ptr_file);
}

void save()
{
	FILE* ptr_file;
	ptr_file = fopen(fat_name, "r+b");
	fseek(ptr_file, sizeof(boot_block), SEEK_SET); // Chega até a FAT.
	fwrite(fat, sizeof(unsigned short), NUM_CLUSTER, ptr_file); // Escreve a FAT.
	fwrite(root_dir, sizeof(dir_entry_t), 32, ptr_file); // Escreve o root_dir.
	fclose(ptr_file);
}

data_cluster get_data_cluster(unsigned index)
{
	FILE* ptr_file;
	data_cluster cluster;
	memset(&cluster, 0x00, CLUSTER_SIZE);
	ptr_file = fopen(fat_name, "rb");
	if (ptr_file == NULL)
	{
		fprintf(stderr, "Não foi possível abrir o arquivo %s.\n", fat_name);
		exit(EXIT_FAILURE);
	}
	fseek(ptr_file, 10 * sizeof(data_cluster), SEEK_SET); // Chega aos clusteres de dados.
	fseek(ptr_file, index * sizeof(data_cluster), SEEK_CUR); // Vai ao índice desejado.
	fread(&cluster, sizeof(data_cluster), 1, ptr_file); // Lê o cluster e coloca no union.
	fclose(ptr_file);
	return cluster;
}

void save_data_cluster(unsigned index, data_cluster cluster)
{
	FILE* ptr_file;
	ptr_file = fopen(fat_name, "r+b");
	if (ptr_file == NULL)
	{
		fprintf(stderr, "Não foi possível abrir o arquivo %s.\n", fat_name);
		exit(EXIT_FAILURE);
	}
	fseek(ptr_file, 10 * sizeof(data_cluster), SEEK_SET); // Chega aos clusteres de dados
	fseek(ptr_file, index * sizeof(data_cluster), SEEK_CUR); // Vai ao índice desejado
	fwrite(&cluster, sizeof(data_cluster), 1, ptr_file); // Lê o union e escreve no cluster
	fclose(ptr_file);
}

bool check_file_existence()
{
	if (access(fat_name, F_OK) != -1)
		return true;
	else
		return false;
}

void free_structure(char*** pieces, unsigned pieces_size)
{
	for (int i = 0; i < pieces_size; i++)
		free((*pieces)[i]);

	free(*pieces);
}
