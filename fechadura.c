#include <stdio.h>
#include <string.h> // <-- NOVO: Necessário para a função de comparação de strings (strcmp)
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/i2c.h"
#include "inc/ssd1306.h"
#include "inc/font.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"


#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C

#define SERVO_MIN_US 550   // Posição de 0 graus
#define SERVO_MAX_US 2300  // Posição de 180 graus

// define o LED de saída
#define GPIO_LED 13
#define servo_pin  20

const char senha_correta[5] = "123A"; // <-- MUDE SUA SENHA DE 4 DÍGITOS AQUI
const char senha_correta1[5] = "123B";
const char senha_correta2[5] = "123C";
const uint button_0 = 5;
static volatile uint32_t last_time = 0; // Armazena o tempo do último evento (em microssegundos)
uint16_t posicao_atual_servo_us;
// define os pinos do teclado com as suas portas GPIO
uint columns[4] = {16, 9, 8, 4};   // Pinos para as colunas
uint rows[4] = {19, 28, 18, 17}; // Pinos para as linhas

//mapa de teclas
char KEY_MAP[16] = {
    '1', '2' , '3', 'A',
    '4', '5' , '6', 'B',
    '7', '8' , '9', 'C',
    '*', '0' , '#', 'D'
};

// As funções do keypad (imprimir_binario, pico_keypad_init, pico_keypad_get_key)
// permanecem exatamente as mesmas.

uint _columns[4];
uint _rows[4];
char _matrix_values[16];
uint all_columns_mask = 0x0;
uint column_mask[4];

void imprimir_binario(int num) {
    int i;
    for (i = 31; i >= 0; i--) {
        (num & (1 << i)) ? printf("1") : printf("0");
    }
}

void pico_keypad_init(uint columns[4], uint rows[4], char matrix_values[16]) {

    for (int i = 0; i < 16; i++) {
        _matrix_values[i] = matrix_values[i];
    }

    for (int i = 0; i < 4; i++) {

        _columns[i] = columns[i];
        _rows[i] = rows[i];

        gpio_init(_columns[i]);
        gpio_init(_rows[i]);

        gpio_set_dir(_columns[i], GPIO_IN);
        gpio_set_dir(_rows[i], GPIO_OUT);

        gpio_put(_rows[i], 1);

        all_columns_mask = all_columns_mask + (1 << _columns[i]);
        column_mask[i] = 1 << _columns[i];
    }
}

char pico_keypad_get_key(void) {
    int row;
    uint32_t cols;
    bool pressed = false;

    cols = gpio_get_all();
    cols = cols & all_columns_mask;
    
    if (cols == 0x0) {
        return 0;
    }

    for (int j = 0; j < 4; j++) {
        gpio_put(_rows[j], 0);
    }

    for (row = 0; row < 4; row++) {

        gpio_put(_rows[row], 1);

        sleep_us(100); 

        cols = gpio_get_all();
        gpio_put(_rows[row], 0);
        cols = cols & all_columns_mask;
        if (cols != 0x0) {
            break;
        }   
    }

    for (int i = 0; i < 4; i++) {
        gpio_put(_rows[i], 1);
    }

    if (cols == column_mask[0]) {
        return (char)_matrix_values[row * 4 + 0];
    }
    else if (cols == column_mask[1]) {
        return (char)_matrix_values[row * 4 + 1];
    }
    if (cols == column_mask[2]) {
        return (char)_matrix_values[row * 4 + 2];
    }
    else if (cols == column_mask[3]) {
        return (char)_matrix_values[row * 4 + 3];
    }
    else {
        return 0;
    }
}

void inicializar_tela(ssd1306_t *disp) {
    // Inicialização do I2C a 400Khz.
    i2c_init(I2C_PORT, 400 * 1000);

    // Define a função dos pinos GPIO para I2C
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA); // Habilita pull-up na linha de dados
    gpio_pull_up(I2C_SCL); // Habilita pull-up na linha de clock

    // Inicializa e configura a estrutura e o hardware do display
    // Note que usamos 'disp' diretamente, pois ele já é um ponteiro
    ssd1306_init(disp, WIDTH, HEIGHT, false, endereco, I2C_PORT);
    ssd1306_config(disp);
    ssd1306_send_data(disp);

    // Limpa o display. O display inicia com todos os pixels apagados.
    ssd1306_fill(disp, false);
    ssd1306_send_data(disp);
}

//Configura e inicializa um canal PWM para controlar um servomotor.
void inicializar_pwm(uint pwm_pin) {
    // 1. Habilita a função PWM para o pino especificado.
    gpio_set_function(pwm_pin, GPIO_FUNC_PWM);

    // 2. Descobre qual "slice" (canal) de PWM controla este pino.
    uint slice_num = pwm_gpio_to_slice_num(pwm_pin);

    // 3. Define o divisor de clock do PWM para obter uma base de contagem
    // que permita gerar 50 Hz. (Clock de 125MHz / 64 = ~1.95 MHz)
    pwm_set_clkdiv(slice_num, 64.0f);

    // 4. Define o valor de "wrap" (o valor máximo do contador).
    // Isso define a frequência final do PWM: 1.95 MHz / 39063 ≈ 50 Hz.
    // O período (duração de um ciclo completo) será de 20ms.
    uint32_t wrap = 39062;
    pwm_set_wrap(slice_num, wrap);

    // 5. Habilita o PWM no slice correspondente.
    // É importante habilitar antes de definir o nível do canal pela primeira vez.
    pwm_set_enabled(slice_num, true);
}

void set_servo_position(uint gpio, uint16_t pulse_width_us) {
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    uint32_t wrap = 39062; // O mesmo valor da inicialização
    // Converte a largura de pulso (µs) para um valor de nível de PWM
    uint32_t level = (uint32_t)((pulse_width_us / 20000.0f) * wrap);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), level);
  
}


void fechada(uint gpio, uint32_t events)
{
    // Obtém o tempo atual em microssegundos
    uint32_t current_time = to_us_since_boot(get_absolute_time());
    // Verifica se passou tempo suficiente desde o último evento
    if (current_time - last_time > 200000) // X ms de debouncing
    {
        last_time = current_time; // Atualiza o tempo do último evento
          set_servo_position(servo_pin, SERVO_MAX_US);                            
    }
}



bool teste_senha(ssd1306_t *disp, char senha_digitada[5]){
    
 if (strcmp(senha_digitada, senha_correta) == 0) {
                    ssd1306_fill(disp, false);
               ssd1306_draw_string(disp, "BEM VINDA", 30, 26); // Desenha uma string
                ssd1306_draw_string(disp, "DANI", 35, 40);
               ssd1306_send_data(disp);
                 sleep_ms(2000); 
                  return true;
                } 
                 else if (strcmp(senha_digitada, senha_correta1) == 0) {
                   ssd1306_fill(disp, false);
               ssd1306_draw_string(disp, "BEM VINDO", 35, 26); // Desenha uma string
                ssd1306_draw_string(disp, "GUILHERME", 30, 40);
               ssd1306_send_data(disp);
                 sleep_ms(2000); 
                  return true;
                } else if (strcmp(senha_digitada, senha_correta2) == 0) {
                   ssd1306_fill(disp, false);
               ssd1306_draw_string(disp, "BEM VINDO", 30, 26); // Desenha uma string
                ssd1306_draw_string(disp, "ADELSON", 35, 40);
               ssd1306_send_data(disp);
                 sleep_ms(2000); 
                  return true;
                } else {
                   ssd1306_fill(disp, false);
               ssd1306_draw_string(disp, "INVALIDO", 30, 30); // Desenha uma string
               ssd1306_send_data(disp);
                 sleep_ms(3000); 
                    gpio_put(GPIO_LED, false); // Mantém ou desliga o LED
                    return false;
                }
}

// --- FUNÇÃO PRINCIPAL MODIFICADA PARA A LÓGICA DE SENHA ---
int main() {
    stdio_init_all();
    pico_keypad_init(columns, rows, KEY_MAP);
    
    gpio_init(GPIO_LED);
    gpio_set_dir(GPIO_LED, GPIO_OUT);
    gpio_put(GPIO_LED, false); // Garante que o LED comece desligado

    gpio_init(button_0);
    gpio_set_dir(button_0, GPIO_IN);    // Configura o pino como entrada
    gpio_pull_up(button_0);  

     gpio_set_irq_enabled_with_callback(button_0, GPIO_IRQ_EDGE_FALL, true, &fechada);


     ssd1306_t ssd;
    inicializar_tela(&ssd);

    inicializar_pwm(servo_pin);


 posicao_atual_servo_us = SERVO_MIN_US;
    set_servo_position(servo_pin, posicao_atual_servo_us);
    sleep_ms(500); // Pequena pausa para garantir que o servo se estabilize


    
    // Variáveis para a lógica da senha
   
    char senha_digitada[5];
    int indice_senha = 0;
    int indice_tela = 48;

    ssd1306_draw_string(&ssd, "DIGITE A SENHA", 8, 10); // Desenha uma string
      ssd1306_send_data(&ssd); // Atualiza o display

    while (true) {
        char caracter_press = pico_keypad_get_key();

        // Só processa se uma tecla foi realmente pressionada
        if (caracter_press != 0) {
            
            // Adiciona o caractere à senha digitada se ainda houver espaço
            if (indice_senha < 4) {
                senha_digitada[indice_senha] = caracter_press;
                indice_senha++;
                indice_tela = indice_tela + 8;
               ssd1306_draw_char(&ssd,caracter_press , indice_tela, 34);
               ssd1306_send_data(&ssd); // Atualiza o display
            }

            // Se 4 dígitos foram inseridos, verifica a senha
            if (indice_senha == 4) {
                senha_digitada[4] = '\0'; // Adiciona o terminador nulo para formar uma string válida

                 ssd1306_fill(&ssd, false);
               ssd1306_draw_string(&ssd, "VERIFICANDO", 30, 32); // Desenha uma string
               ssd1306_send_data(&ssd); // Atualiza o display
                 sleep_ms(2000); 
                // Compara a senha digitada com a senha correta
               
          if (teste_senha(&ssd, senha_digitada)) {
                               gpio_put(GPIO_LED, true); // Acende o LED
                            set_servo_position(servo_pin, SERVO_MIN_US);
                         
             }
                // Reseta o índice para a próxima tentativa
                indice_senha = 0;
                indice_tela = 48;
                 ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "DIGITE A SENHA", 8, 10); // Desenha uma string
      ssd1306_send_data(&ssd); // Atualiza o display
               
     
            }
        }
        
        // Pequeno delay para evitar leituras múltiplas da mesma tecla
        sleep_ms(200); 
    }
}