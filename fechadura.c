#include <stdio.h>                            // Biblioteca padrão de entrada e saída
#include <string.h>                           // Necessária para comparação de strings (strcmp)
#include "pico/stdlib.h"                      // Biblioteca principal do Pico SDK
#include "hardware/timer.h"                   // Controle de temporizadores
#include "hardware/i2c.h"                     // Comunicação I2C
#include "inc/ssd1306.h"                      // Biblioteca do display OLED
#include "inc/font.h"                         // Fonte usada para o display
#include "hardware/pwm.h"                     // Controle de PWM para servo
#include "hardware/clocks.h"                  // Controle de clock
#include "hardware/adc.h"                     // Leitura do sensor de temperatura via ADC

#define I2C_PORT i2c1                           // Define o periférico I2C utilizado
#define I2C_SDA 14                              // Pino SDA do barramento I2C
#define I2C_SCL 15                              // Pino SCL do barramento I2C
#define endereco 0x3C                           // Endereço I2C do display OLED SSD1306
#define SERVO_MIN_US 550                        // Largura de pulso correspondente à posição mínima do servo (0 graus)
#define SERVO_MAX_US 2300                       // Largura de pulso correspondente à posição máxima do servo (180 graus)

#define ADC_TEMPERATURE_CHANNEL 4              // Canal 4 do ADC corresponde ao sensor de temperatura interno

#define GPIO_LED1 13                            // Pino do LED de erro (vermelho)
#define GPIO_LED 11                             // Pino do LED de sucesso (verde)
#define servo_pin  20                           // Pino PWM conectado ao servo motor

const char senha_correta[5] = "123A";           // Senha válida para a primeira pessoa autorizada (DANI)
const char senha_correta1[5] = "123D";          // Senha válida para a segunda pessoa autorizada (GUILHERME)
const char senha_correta2[5] = "123C";          // Senha válida para a terceira pessoa autorizada (ADELSON)

const uint button_0 = 5;                        // Pino conectado ao botão físico externo (para abrir)
static volatile uint64_t last_time = 0;         // Tempo da última ativação por interrupção (debounce)
uint16_t posicao_atual_servo_us;                // Guarda a posição atual do servo motor

uint columns[4] = {16, 9, 8, 4};                // Pinos que correspondem às colunas do teclado matricial
uint rows[4] = {19, 28, 18, 17};                // Pinos que correspondem às linhas do teclado matricial

char KEY_MAP[16] = {                            // Mapeamento das teclas do teclado 4x4
    '1', '2', '3', 'A',
    '4', '5', '6', 'B',
    '7', '8', '9', 'C',
    '*', '0', '#', 'D'
};

uint _columns[4];                               // Variáveis internas para armazenar colunas
uint _rows[4];                                  // Variáveis internas para armazenar linhas
char _matrix_values[16];                        // Mapeamento interno das teclas
uint all_columns_mask = 0x0;                    // Máscara para detectar colunas ativas
uint column_mask[4];                            // Máscara individual de cada coluna

// Função que imprime um número inteiro de 32 bits em binário
void imprimir_binario(int num) {
    for (int i = 31; i >= 0; i--) {
        (num & (1 << i)) ? printf("1") : printf("0");
    }
}

// Função para inicializar o teclado matricial
void pico_keypad_init(uint columns[4], uint rows[4], char matrix_values[16]) {
    for (int i = 0; i < 16; i++) {
        _matrix_values[i] = matrix_values[i];   // Copia o mapa de teclas
    }

    for (int i = 0; i < 4; i++) {
        _columns[i] = columns[i];              // Armazena pinos de coluna
        _rows[i] = rows[i];                    // Armazena pinos de linha
        gpio_init(_columns[i]);                // Inicializa pino de coluna
        gpio_init(_rows[i]);                   // Inicializa pino de linha
        gpio_set_dir(_columns[i], GPIO_IN);    // Define coluna como entrada
        gpio_set_dir(_rows[i], GPIO_OUT);      // Define linha como saída
        gpio_put(_rows[i], 1);                 // Mantém linha em nível alto
        all_columns_mask += (1 << _columns[i]); // Atualiza máscara geral
        column_mask[i] = 1 << _columns[i];     // Atualiza máscara da coluna individual
    }
}

// Função que retorna o caractere pressionado no teclado matricial
char pico_keypad_get_key(void) {
    int row;
    uint32_t cols = gpio_get_all() & all_columns_mask; // Lê todas as colunas
    if (cols == 0x0) return 0;                 // Nenhuma tecla pressionada

    for (int j = 0; j < 4; j++) gpio_put(_rows[j], 0); // Zera todas as linhas

    for (row = 0; row < 4; row++) {
        gpio_put(_rows[row], 1);              // Ativa uma linha por vez
        sleep_us(100);                         // Aguarda estabilização
        cols = gpio_get_all() & all_columns_mask; // Lê as colunas
        gpio_put(_rows[row], 0);               // Desativa a linha
        if (cols != 0x0) break;                // Se encontrou tecla pressionada, sai do loop
    }

    for (int i = 0; i < 4; i++) gpio_put(_rows[i], 1); // Restaura as linhas

    for (int i = 0; i < 4; i++) {              // Identifica qual coluna foi pressionada
        if (cols == column_mask[i]) return _matrix_values[row * 4 + i];
    }
    return 0;                                  // Nenhuma tecla válida detectada
}

// Inicializa o display OLED
void inicializar_tela(ssd1306_t *disp) {
    i2c_init(I2C_PORT, 400 * 1000);            // Inicia I2C com frequência de 400kHz
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); // Configura função I2C no pino SDA
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); // Configura função I2C no pino SCL
    gpio_pull_up(I2C_SDA);                     // Ativa pull-up no SDA
    gpio_pull_up(I2C_SCL);                     // Ativa pull-up no SCL

    ssd1306_init(disp, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa display
    ssd1306_config(disp);                      // Configura parâmetros do display
    ssd1306_send_data(disp);                   // Envia dados de configuração
    ssd1306_fill(disp, false);                // Limpa a tela
    ssd1306_send_data(disp);                   // Atualiza display
}

// Configura e inicializa PWM para controle de servo motor
void inicializar_pwm(uint pwm_pin) {
    gpio_set_function(pwm_pin, GPIO_FUNC_PWM); // Define função PWM
    uint slice_num = pwm_gpio_to_slice_num(pwm_pin); // Obtém o slice
    pwm_set_clkdiv(slice_num, 64.0f);          // Reduz a frequência para ~50Hz
    pwm_set_wrap(slice_num, 39062);            // Define período de 20ms (1.95 MHz / 39062)
    pwm_set_enabled(slice_num, true);          // Ativa PWM
}

// Converte leitura do ADC para temperatura em Celsius
float adc_to_temperature(uint16_t adc_value) {
    const float conversion_factor = 3.3f / (1 << 12); // Conversão de 12 bits (0-4095)
    float voltage = adc_value * conversion_factor;    // Converte ADC para tensão
    return 27.0f - (voltage - 0.706f) / 0.001721f;     // Fórmula do datasheet
}

// Define posição do servo com base na largura de pulso
void set_servo_position(uint gpio, uint16_t pulse_width_us) {
    uint slice_num = pwm_gpio_to_slice_num(gpio);     // Obtém slice do PWM
    uint32_t wrap = 39062;                             // Valor de wrap do PWM
    uint32_t level = (uint32_t)((pulse_width_us / 20000.0f) * wrap); // Converte µs para nível
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), level); // Define nível do canal
}

// Função chamada pela interrupção do botão
void fechada(uint gpio, uint32_t events) {
    uint64_t current_time = to_us_since_boot(get_absolute_time()); // Tempo atual
    if (current_time - last_time > 200000) {   // Verifica debounce (200ms)
        last_time = current_time;              // Atualiza tempo
        set_servo_position(servo_pin, SERVO_MAX_US); // Fecha a tranca
    }
}

// Declaração da função 'teste_senha' que retorna um booleano (true/false)
// Ela recebe como parâmetros um ponteiro para o display (disp) e a senha digitada.
bool teste_senha(ssd1306_t *disp, char senha_digitada[5]) {

    // Compara a senha digitada com a primeira senha correta. 'strcmp' retorna 0 se forem iguais.
    if (strcmp(senha_digitada, senha_correta) == 0) {
        ssd1306_fill(disp, false);                       // Limpa a tela do display (preenche com cor de fundo).
        ssd1306_draw_string(disp, "BEM VINDA", 30, 26);   // Escreve a string "BEM VINDA" na posição (30, 26).
        ssd1306_draw_string(disp, "DANI", 51, 40);        // Escreve a string "DANI" na posição (51, 40).
        ssd1306_send_data(disp);                         // Envia os dados do buffer para o display, atualizando a tela.
        gpio_put(GPIO_LED, true);                        // Acende o LED conectado ao pino GPIO_LED.
        sleep_ms(2000);                                  // Pausa a execução do programa por 2000 milissegundos (2 segundos).
        return true;                                     // Retorna 'true', indicando que a senha estava correta.
    }
    // Se a primeira senha estiver errada, compara a senha digitada com a segunda senha correta.
    else if (strcmp(senha_digitada, senha_correta1) == 0) {
        ssd1306_fill(disp, false);                       // Limpa a tela do display.
        ssd1306_draw_string(disp, "BEM VINDO", 35, 26);   // Escreve "BEM VINDO" na posição (35, 26).
        ssd1306_draw_string(disp, "GUILHERME", 30, 40);   // Escreve "GUILHERME" na posição (30, 40).
        ssd1306_send_data(disp);                         // Atualiza a tela do display.
        gpio_put(GPIO_LED, true);                        // Acende o LED de sucesso.
        sleep_ms(2000);                                  // Pausa por 2 segundos.
        return true;                                     // Retorna 'true' (senha correta).
    }
    // Se as duas primeiras estiverem erradas, compara com a terceira senha correta.
    else if (strcmp(senha_digitada, senha_correta2) == 0) {
        ssd1306_fill(disp, false);                       // Limpa a tela do display.
        ssd1306_draw_string(disp, "BEM VINDO", 30, 26);   // Escreve "BEM VINDO" na posição (30, 26).
        ssd1306_draw_string(disp, "ADELSON", 35, 40);     // Escreve "ADELSON" na posição (35, 40).
        ssd1306_send_data(disp);                         // Atualiza a tela do display.
        gpio_put(GPIO_LED, true);                        // Acende o LED de sucesso.
        sleep_ms(2000);                                  // Pausa por 2 segundos.
        return true;                                     // Retorna 'true' (senha correta).
    }
    // Se nenhuma das senhas anteriores for a correta, executa este bloco.
    else {
        ssd1306_fill(disp, false);                       // Limpa a tela do display.
        ssd1306_draw_string(disp, "INVALIDO", 30, 30);    // Escreve "INVALIDO" na posição (30, 30).
        ssd1306_send_data(disp);                         // Atualiza a tela do display.
        gpio_put(GPIO_LED1, true);                       // Acende um LED de aviso/erro diferente (GPIO_LED1).
        sleep_ms(2000);                                  // Pausa por 2 segundos.
        return false;                                    // Retorna 'false', indicando que a senha estava incorreta.
    }
}
// --- FUNÇÃO PRINCIPAL ---
int main() {
    // --- Bloco de Inicializações Gerais ---
    stdio_init_all();                         // Inicializa todas as E/S padrão (essencial para comunicação USB serial)
    pico_keypad_init(columns, rows, KEY_MAP); // Inicializa o teclado matricial com os pinos e mapa de teclas definidos

    // --- Configuração do LED de Sucesso ---
    gpio_init(GPIO_LED);                      // Inicializa o pino GPIO para o LED
    gpio_set_dir(GPIO_LED, GPIO_OUT);         // Configura o pino do LED como uma saída
    gpio_put(GPIO_LED, false);                // Garante que o LED de sucesso comece desligado

    // --- Configuração do LED de Erro/Aviso ---
    gpio_init(GPIO_LED1);                     // Inicializa o pino GPIO para o segundo LED
    gpio_set_dir(GPIO_LED1, GPIO_OUT);        // Configura o pino do LED como uma saída
    gpio_put(GPIO_LED1, false);               // Garante que o LED de erro comece desligado

    // --- Configuração do Botão ---
    gpio_init(button_0);                      // Inicializa o pino GPIO para o botão
    gpio_set_dir(button_0, GPIO_IN);          // Configura o pino do botão como uma entrada
    gpio_pull_up(button_0);                   // Habilita o resistor de pull-up interno para evitar leituras "flutuantes"

    // --- Configuração do Sensor de Temperatura Interno ---
    adc_init();                               // Inicializa o módulo ADC (Conversor Analógico-Digital)
    adc_set_temp_sensor_enabled(true);        // Habilita o sensor de temperatura que já vem no chip do Pico
    adc_select_input(ADC_TEMPERATURE_CHANNEL);// Seleciona o canal do ADC correspondente a esse sensor

    // --- Configuração da Interrupção do Botão ---
    // Configura o pino do botão para gerar uma interrupção na borda de descida (quando o botão é pressionado)
    // e chama a função 'fechada' quando isso acontecer.
    gpio_set_irq_enabled_with_callback(button_0, GPIO_IRQ_EDGE_FALL, true, &fechada);

    // --- Configuração do Display OLED ---
    ssd1306_t ssd;                            // Cria uma instância da estrutura de controle do display
    inicializar_tela(&ssd);                   // Chama a função para configurar e limpar a tela

    // --- Configuração do Servo Motor ---
    inicializar_pwm(servo_pin);               // Inicializa o sinal PWM no pino conectado ao servo
    posicao_atual_servo_us = SERVO_MIN_US;    // Define a variável de posição para a posição mínima inicial
    set_servo_position(servo_pin, posicao_atual_servo_us); // Manda o servo para a posição inicial
    sleep_ms(500);                            // Pequena pausa para garantir que o servo chegue à posição

    // --- Declaração de Variáveis para a Lógica Principal ---
    char senha_digitada[5];                   // Array para armazenar os 4 dígitos da senha e o caractere nulo '\0'
    int indice_senha = 0;                     // Contador para saber quantos dígitos já foram inseridos
    int indice_tela = 48;                     // Coordenada X para exibir os dígitos na tela, evitando sobreposição
    char temperatura[15];                     // String para armazenar o valor da temperatura já formatado para exibição

    // Variáveis para a atualização periódica da temperatura sem usar 'sleep'
    uint64_t last_temp_update_time = 0;       // Guarda o tempo da última atualização
    const uint64_t temp_update_interval_us = 60000000; // Intervalo de atualização: 60 segundos em microssegundos

    // --- Loop Principal (executa para sempre) ---
    while (true) {
        // --- Bloco de Atualização Periódica da Temperatura ---
        uint64_t current_time = to_us_since_boot(get_absolute_time()); // Pega o tempo atual desde que o Pico ligou
        
        // Verifica se já passou o intervalo de 60s ou se é a primeira vez que o loop roda
        if (current_time - last_temp_update_time > temp_update_interval_us || last_temp_update_time == 0) {
            uint16_t adc_value = adc_read();                      // Lê o valor "cru" do sensor
            float temp_value = adc_to_temperature(adc_value);     // Converte o valor lido para graus Celsius
            sprintf(temperatura, "%.1fC", temp_value);            // Formata a string para "XX.XC"
            last_temp_update_time = current_time;                 // Salva o tempo atual como o da última atualização
        }

        // --- Leitura do Teclado e Atualização da Tela Principal ---
        char caracter_press = pico_keypad_get_key();      // Verifica se alguma tecla foi pressionada
        
        ssd1306_draw_string(&ssd, "DIGITE A SENHA", 8, 18);// Exibe a instrução principal
        ssd1306_draw_string(&ssd, temperatura, 80, 2);    // Exibe a temperatura no canto superior direito
        ssd1306_pixel(&ssd, 100, 8, 1);                   // Desenha um pixel (ponto de separação da casa decimal)
        ssd1306_send_data(&ssd);                          // Envia todas as informações desenhadas para o display

        // --- Bloco de Processamento da Senha ---
        if (caracter_press != 0) { // Executa apenas se uma tecla foi realmente pressionada
            
            // Adiciona o caractere à senha se ainda não tivermos 4 dígitos
            if (indice_senha < 4) {
                senha_digitada[indice_senha] = caracter_press;      // Armazena o dígito no array
                indice_senha++;                                     // Incrementa o contador de dígitos
                indice_tela += 8;                                   // Move a posição de escrita na tela para o próximo caractere
                ssd1306_draw_char(&ssd, caracter_press, indice_tela, 42); // Exibe o dígito na tela
                ssd1306_send_data(&ssd);                            // Atualiza a tela para mostrar o dígito imediatamente
            }

            // Se 4 dígitos já foram inseridos, é hora de verificar a senha
            if (indice_senha == 4) {
                senha_digitada[4] = '\0';                 // Adiciona o terminador nulo para transformar o array em uma string válida

                ssd1306_fill(&ssd, false);                // Limpa a tela
                ssd1306_draw_string(&ssd, "VERIFICANDO", 30, 32); // Mostra uma mensagem de "Verificando"
                ssd1306_send_data(&ssd);                  // Atualiza a tela com a mensagem
                sleep_ms(2000);                           // Espera 2 segundos para o usuário ler

                // Chama a função externa para testar a senha. Se ela retornar 'true'...
                if (teste_senha(&ssd, senha_digitada)) {
                    gpio_put(GPIO_LED, false);            // Apaga o LED de sucesso (após a mensagem de boas-vindas)
                    set_servo_position(servo_pin, SERVO_MIN_US); // Move o servo de volta para a posição mínima
                }

                // --- Reseta as variáveis para a próxima tentativa de senha ---
                indice_senha = 0;                         // Zera o contador de dígitos
                indice_tela = 48;                         // Retorna a posição de escrita para o início
                gpio_put(GPIO_LED1, false);               // Garante que o LED de erro esteja apagado
                ssd1306_fill(&ssd, false);                // Limpa a tela para a próxima digitação
            }
        }
        
        // Pequeno delay para evitar leituras múltiplas da mesma tecla (debouncing por software)
        // e para não sobrecarregar o processador.
        sleep_ms(200); 
    }
}