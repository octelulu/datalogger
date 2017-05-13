/* Projeto de um Datallogger usando kernel de tempo real
  Um datalogger é um dispoditivo de aquisição e gravação de dados ao longo do tempo que podem ser recuperados.
  O sistema consiste em um sensor de luminosidade e uma memória EEPROM para salvar os dados medidos.
  O sistema pode ser operado conectado a um computador pelo terminal ou remotamente usando um teclado matricial.

  Uso:
  - Computador envia (pelo terminal) uma mensagem:
  PING\n\n
  - MCU retorna (no terminal):
  PONG\n
  
  - Computador envia (pelo terminal) uma mensagem:
  ID\n\n
  - MCU retorna (no terminal):
  String de Identificação do sistema
  
  - Computador envia (pelo terminal) uma mensagem:
  MEASURE\n\n
  - MCU retorna (no terminal):
  Medida no sensor de luminosidade
  
  - Computador envia (pelo terminal) uma mensagem:
  MEMSTATUS\n\n
  - MCU retorna (no terminal):
  Número de elementos salvos na EEPROM
  
  - Computador envia (pelo terminal) uma mensagem:
  RESET\n\n
  - MCU apaga toda a EEPROM
  
  - Computador envia (pelo terminal) uma mensagem:
  RECORD\n\n
  - MCU salva na EEPROM o valor medido pelo sensor de luminosidade
  
  - Computador envia (pelo terminal) uma mensagem:
  GET N\n\n
  - MCU retorna, caso exista, o valor salvo na posição N
  
  - Comando eviado pelo teclado: 
  #1*
  - MCU pisca um LED para demonstrar que está responsivo
  
  - Comando eviado pelo teclado: 
  #2*
  - MCU salva na EEPROM o valor medido pelo sensor de luminosidade
  
  - Comando eviado pelo teclado: 
  #3*
  - MCU inicia medição automática de luminosidade
  
  - Comando eviado pelo teclado: 
  #4*
  - MCU encerra medição automática de luminosidade
  
 */

/* stdio.h contem rotinas para processamento de expressoes regulares */
#include <stdio.h>
#include <Wire.h>
#include <TimerOne.h>

/* Flags globais para controle de processos da interrupcao */
byte flag_check_command = 0;
bool flag_automatic = false;
bool flag_record = false;
bool debounce = false;

// Associação dos número das portas digitais do Arduino com seu significado físico
const byte Linha_1 = 2;
const byte Linha_2 = 3;
const byte Linha_3 = 4;
const byte Linha_4 = 5;
const byte Coluna_1 = 6;
const byte Coluna_2 = 7;
const byte Coluna_3 = 8;
const byte led = 11;

// Device Address da EEPROM
const byte EEPROM_ADDRESS = 0x50;

// Máquina de Estados
byte keyboard_state=0;
char comando = '0';

// Pino e medida do conversor analógico/digital
int sensorPin = A0;
int sensorValue = 0;

// Contadores
byte counter;
byte automatic_measure_counter=10;


/* Rotina auxiliar para comparacao de strings */
int str_cmp(char *s1, char *s2, int len) {
  /* Compare two strings up to length len. Return 1 if they are
   *  equal, and 0 otherwise.
   */
  int i;
  for (i=0; i<len; i++) {
    if (s1[i] != s2[i]) return 0;
    if (s1[i] == '\0') return 1;
  }
  return 1;
}

/* Processo de bufferizacao. Caracteres recebidos sao armazenados em um buffer. Quando um caractere
 *  de fim de linha ('\n') e recebido, todos os caracteres do buffer sao processados simultaneamente.
 */

/* Buffer de dados recebidos */
#define MAX_BUFFER_SIZE 15
typedef struct {
  char data[MAX_BUFFER_SIZE];
  unsigned int tam_buffer;
} serial_buffer;

/* Teremos somente um buffer em nosso programa, O modificador volatile
 *  informa ao compilador que o conteudo de Buffer pode ser modificado a qualquer momento. Isso
 *  restringe algumas otimizacoes que o compilador possa fazer, evitando inconsistencias em
 *  algumas situacoes (por exemplo, evitando que ele possa ser modificado em uma rotina de interrupcao
 *  enquanto esta sendo lido no programa principal).
 */
volatile serial_buffer Buffer;

/* Todas as funcoes a seguir assumem que existe somente um buffer no programa e que ele foi
 *  declarado como Buffer. Esse padrao de design - assumir que so existe uma instancia de uma
 *  determinada estrutura - se chama Singleton (ou: uma adaptacao dele para a programacao
 *  nao-orientada-a-objetos). Ele evita que tenhamos que passar o endereco do
 *  buffer como parametro em todas as operacoes (isso pode economizar algumas instrucoes PUSH/POP
 *  nas chamadas de funcao, mas esse nao eh o nosso motivo principal para utiliza-lo), alem de
 *  garantir um ponto de acesso global a todas as informacoes contidas nele.
 */

/* Limpa buffer */
void buffer_clean() {
  Buffer.tam_buffer = 0;
}

/* Adiciona caractere ao buffer */
int buffer_add(char c_in) {
  if (Buffer.tam_buffer < MAX_BUFFER_SIZE) {
    Buffer.data[Buffer.tam_buffer++] = c_in;
    return 1;
  }
  return 0;
}

/* Escreve um byte em uma posição definida da EEPROM */
void write_one_byte(byte device_address, byte memory_position, byte data) {
        Wire.beginTransmission(device_address);
        Wire.write(memory_position);
        Wire.write(data);
        Wire.endTransmission();
        delay(5);
}

/* Lê um byte de uma posição definida da EEPROM */
byte read_one_byte(byte device_address, byte memory_position) {
        byte data;
  
        Wire.beginTransmission(device_address);
        Wire.write(memory_position);
        Wire.endTransmission();
        delay(5);
        
        Wire.requestFrom(device_address, 1);
        if (Wire.available()){
          data = Wire.read();
        }
        delay(5);
        return data;
}

/* Salva um byte no sistema de arquivo da EEPROM */
void save_eeprom(byte data) {
  byte n_elements;
  // A posição 0 da EEPROM armazena o número de elementos salvos na memória
  n_elements = read_one_byte(EEPROM_ADDRESS, 0);
  // Como EPROM_ADDRESS foi declarado constante, consegue-se acessar apenas 256 posições da memória: de 0 a 255
  if(n_elements<255) {
    write_one_byte(EEPROM_ADDRESS, n_elements+1, data);
    write_one_byte(EEPROM_ADDRESS, 0, n_elements+1); // Atualiza o número de elementos de acordo com sistema de arquivos
  }
}

/* Valida se uma posição de memória pode ser lida */
bool isReadable(byte memPosition) {
  byte n_elements;

  n_elements = read_one_byte(EEPROM_ADDRESS, 0);
  if(memPosition >= 1 && memPosition <= n_elements) {
    return true;
  }
  else {
    return false;
  }
}

/* Mapeia os números dos pinos do Arduino conectados as linhas e colunas do teclado à seu caractere correspondente */
char keyboardMap(byte linha, byte coluna) {
  if(linha == 2) {
    if(coluna == 6)       return '1';
    else if(coluna == 7)  return '2';
    else if(coluna == 8)  return '3';
  }
  if(linha == 3) {
    if(coluna == 6)       return '4';
    else if(coluna == 7)  return '5';
    else if(coluna == 8)  return '6';
  }
  if(linha == 4) {
    if(coluna == 6)       return '7';
    else if(coluna == 7)  return '8';
    else if(coluna == 8)  return '9';
  }
  if(linha == 5) {
    if(coluna == 6)       return '#';
    else if(coluna == 7)  return '0';
    else if(coluna == 8)  return '*';
  }
  else return '\0';
}

/* Varre as linhas e colunas para detecção do botão pressionado */
char sweep() {
  byte linha, coluna;
  for (linha=2; linha<=5; linha++) {
    digitalWrite(linha, LOW);
    for (coluna=6; coluna<=8; coluna++) {
      if (digitalRead(coluna) == LOW) {
        digitalWrite(linha, HIGH);
        return keyboardMap(linha, coluna);
      }
    }
    digitalWrite(linha, HIGH);
  }
  return '\0';
}

/* 
Máquina de estados para interpretação dos comandos do teclado
#: habilita o recebimento do número do comando
[1-4]: Códigos dos comandos
*: Executa um comando
A máquina retorna um char diferente para cada comando
 */
char parser(char c){
 
  switch(keyboard_state){
    // Estado inicial
    case 0:
      if(c=='#'){
        keyboard_state=1;
      }
      else keyboard_state=0;
      break;
      
    // Estado habilitado para receber comandos
    case 1:
      if(c=='#') keyboard_state=1;
      else if(c=='1'||c=='2'||c=='3'||c=='4'){
        comando=c;
        keyboard_state=2;
      }
      else keyboard_state=0;
      break;
      
    // Estado habilitado para executar comando
    case 2:
      if(c=='*'){
        keyboard_state=0;
        return comando;
      }
      else if(c=='#') keyboard_state=1;
      else keyboard_state=0;
      break;

  }  
  return '0'; 
}

/* Evia entrada para o parser e, caso haja saída, executa as ações associadas */
void checkCommand(char c){
  
  switch(parser(c)){
    case '0':
      break; 
    case '1':
      ledBlink();
      break;
    case '2':
      flag_record = true;
      break;
    case '3':
      flag_record = true;
      flag_automatic = true;
      break;
    case '4':
      flag_automatic = false;
      break;
  }
}

/* Função que faz o LED piscar */
void ledBlink() {
    digitalWrite(led, HIGH);
    delay(100000);
    digitalWrite(led, LOW);
    delay(100000);
    digitalWrite(led, HIGH);
    delay(100000);
    digitalWrite(led, LOW);
}

/* Rotinas de interrupcao */

/* Ao receber evento da UART */
void serialEvent() {
  char c;
  while (Serial.available()) {
    c = Serial.read();
    if (c=='\n') {
      buffer_add('\0'); /* Se recebeu um fim de linha, coloca um terminador de string no buffer */
      flag_check_command = 1;
    } else {
     buffer_add(c);
    }
  }
}

/* Rotina da interrupção de tempo */
void timeInterrupt() {
  char keyboard_input;
  
  // Caso o sistema não esteja em debounce, varre o teclado
  // Caso encontre um caractere válido, entra em debounce e o envia para o parser
  if(!debounce) {
    keyboard_input = sweep();
    if(keyboard_input != '\0') {
      debounce = true;
      Serial.print(keyboard_input);
      Serial.print('\n');
      checkCommand(keyboard_input);
      counter = 2;
    }
  }

  // Timer para debounce
  else {
    counter--;
    if(counter == 0) {
      debounce = false;
    }
  }

  // Timer para definir taxa de amostragem da medição automática
  if(flag_automatic){
    automatic_measure_counter--;
    if(automatic_measure_counter==0){
      flag_record=true;
      automatic_measure_counter=10;
    }
  }
}

/* Funcoes internas ao void main() */

void setup() {
  /* Inicializacao */
  buffer_clean();
  flag_check_command = 0;
  Serial.begin(9600);
  Wire.begin();
  
  // Inicializa o sistema de arquivos sem nenhum elemento salvo no EEPROM
  write_one_byte(EEPROM_ADDRESS, 0, 0);

  // Definição do modo das portas digitais
  pinMode(led, OUTPUT);
  pinMode(Linha_1, OUTPUT);
  pinMode(Linha_2, OUTPUT);
  pinMode(Linha_3, OUTPUT);
  pinMode(Linha_4, OUTPUT);
  pinMode(Coluna_1, INPUT);
  pinMode(Coluna_2, INPUT);
  pinMode(Coluna_3, INPUT);
  
  // Definição do estado inicial das saídas digitais
  digitalWrite(Linha_1, HIGH);
  digitalWrite(Linha_2, HIGH);
  digitalWrite(Linha_3, HIGH);
  digitalWrite(Linha_4, HIGH);

  digitalWrite(led, LOW);
  
  // Configuração da interrupção de tempo
  Timer1.initialize(100000); 
  Timer1.attachInterrupt( timeInterrupt );
}


void loop() {
  int x, y, memory_position;
  char out_buffer[10];
  int flag_write = 0;
  byte elementos, n_elements;
  byte measure;

  /* A flag_check_command permite separar a recepcao de caracteres
   *  (vinculada a interrupca) da interpretacao de caracteres. Dessa forma,
   *  mantemos a rotina de interrupcao mais enxuta, enquanto o processo de
   *  interpretacao de comandos - mais lento - nao impede a recepcao de
   *  outros caracteres. Como o processo nao 'prende' a maquina, ele e chamado
   *  de nao-preemptivo.
   */
  if (flag_check_command == 1) {
    if (str_cmp(Buffer.data, "PING", 4) ) {
      Serial.print("PONG\n");
    }

    if (str_cmp(Buffer.data, "ID", 2) ) {
      Serial.print("OCTAVIO E LUÍS\n");
    }

    if (str_cmp(Buffer.data, "MEASURE", 7) ) {
      sensorValue = (byte)analogRead(sensorPin);
      sprintf(out_buffer, "%d\n", sensorValue);
      Serial.write(out_buffer);
    }

    if (str_cmp(Buffer.data, "MEMSTATUS", 9) ) {
      elementos = read_one_byte(EEPROM_ADDRESS, 0);
      sprintf(out_buffer, "Elements: %d\n", elementos);
      Serial.write(out_buffer);
    }

    if (str_cmp(Buffer.data, "RECORD", 6)) {
      measure = (byte)analogRead(sensorPin);
      save_eeprom(measure);
      Serial.print("Recorded\n");
    }

    if (str_cmp(Buffer.data, "RESET", 5) ) {
      write_one_byte(EEPROM_ADDRESS, 0, 0);
      Serial.print("You lost everything!\n");
    }

    if (str_cmp(Buffer.data, "GET", 3) ) {
      sscanf(Buffer.data, "%*s %d", &memory_position);
      if(!isReadable(memory_position)) {
        Serial.print("Invalid memory position\n");
      }
      else {
        elementos = read_one_byte(EEPROM_ADDRESS, memory_position);
        sprintf(out_buffer, "%d\n", elementos);
        Serial.write(out_buffer);
      }
    }
    
    flag_check_command = 0;
    buffer_clean();
  }
  
  
  /* A flag_record habilita a gravação na memória pelo comando do teclado */
  if(flag_record) {
    measure = (byte)analogRead(sensorPin);
    save_eeprom(measure);
    Serial.print("Recorded\n");
    flag_record = false;  
  }
}
