#include <mega128a.h>
#include <stdio.h>
#include <delay.h>
#include <twi.h>
#include <alcd_twi.h>

// 매크로 선언
#define SW_wash PIND.4
#define SW_rinse PIND.5
#define SW_dry PIND.6
#define LED_run 0xfe
#define LED_pause 0xfd
#define LED_end 0xfb

// 전역변수 선언 
bit WashingMachine_ON = 0;
bit Start = 0;
bit pause = 0;
bit PushFlg = 0;
bit WM_flag = 0;
bit modesel_flag = 0;
bit washcnt_flag = 0;
bit Start_flag = 0;
bit selfmode_flag = 0;
unsigned char save_tcntH;
unsigned char save_tcntL;
char alarm[6][20]= {"POWER ON", "START  Washing  ", "START   Rinse   ", "START    Dry    ", "      STOP      ", " End of washing "};
char mode[4][20]={"STANDARD MODE   ", "POWER MODE     ", "SPEED MODE      ", "SELF MODE       "};
unsigned char wash_count[4][3]={ {2, 1, 1}, {2, 2, 2}, {1, 1, 1}, {0, 0, 0} };   // [st, po, sp, sl][wash, rin, dry]

unsigned char adc_data = 0xff;

// 시간저장을 위한 전역변수
unsigned char msec = 0;
unsigned char sec = 0;
unsigned char min = 0;
unsigned char hour = 0;
unsigned char time_arr[3];   // wash, rinse, dry
unsigned char pause_time[3]; // 시, 분, 초

// External Interrupt 2 service routine
/*
세탁기 ON / 모드 선택 / 시작 / 일시정지 선택
falling edge
*/
interrupt [EXT_INT2] void ext_int2_isr(void)
{
    if(WashingMachine_ON == 0)  // 세탁기 ON
    {
        WashingMachine_ON = 1;
        WM_flag = 1;
    }
    else if(WM_flag == 1&& modesel_flag == 1)       // 모드 설정
    {
        WM_flag = 0;
    }
    else if(selfmode_flag == 1 && washcnt_flag == 1) // 셀프 모드 설정
    {
        selfmode_flag = 0;
        Start = 1;
        Start_flag = 1;
    }
    else if(Start == 0)         // 시작
    {   
        Start = 1;
        Start_flag = 1;
    }
    else if(Start == 1)         // 일시 정지
    { 
        if(pause == 0)
        {
            pause = 1;
            TIMSK=0x00;
            TCCR1B=0x08;
            TCCR2=0x4b;
            save_tcntH = TCNT1H;
            save_tcntL = TCNT1L;
            pause_time[0] = hour;
            pause_time[1] = min;
            pause_time[2] = sec;
        }
        else if(pause == 1)     // 일시 정지 해제
        {
            pause = 0;
            TCCR1B=0x02;
            TCCR2=0x7b;
            TIMSK = 0x04;
            hour = pause_time[0]; 
            min = pause_time[1];
            sec = pause_time[2];
            TCNT1H=save_tcntH;
            TCNT1L=save_tcntL;// 10msec
        }
    }
}

 // 타이머0 비교 플로우 - 버저
interrupt [TIM0_COMP] void timer0_comp_isr(void)
{
    TCNT0=0x0006; 
}

// 타이머 1 인터럽트 설정
interrupt [TIM1_OVF] void timer1_ovf_isr(void) 
{
    // Reinitialize Timer1 value
    TCNT1H=0xB1;
    TCNT1L=0xE0;
    msec++;
    
    if(msec == 100)
    {   
        msec = 0;
        if(sec ==0)
        {
            sec = 59;
            if(min == 0)
            {
                min = 59;
                if(hour == 0)
                {
                    TCCR1B=0x08;
                    TIMSK=0x00;
                }
                else hour--;
            }
            else    min--;
        }
        else sec--;
    }
}

// 타이머2 - 모터 출력을 위한 인터럽트 서비스 루틴
interrupt [TIM2_COMP] void timer2_comp_isr(void)
{
    TCNT2=0x0006; 
}

// ADC
interrupt [ADC_INT] void adc_isr(void)
{
#asm("cli");
// Read the AD conversion result
adc_data=ADCL;
adc_data=ADCH;
// Place your code here
#asm("sei");
}

// Buzzer 함수
void buzzer(float hz, int count){
        int j;
        for(j=0; j<count; j++){
            PORTB = 0x10;
            delay_ms(((float)1000/hz)/2);
            
            PORTB = 0x00;
            delay_ms(((float)1000/hz)/2);
        }
}

void wm_init(void)
{
    // flag 초기화
    Start = 0;
    WashingMachine_ON = 0;
    washcnt_flag = 0;
    modesel_flag = 0;
                    
    // LED OFF
    PORTA = 0xff;
                    
    // 셀프 세탁 카운트 초기화
    wash_count[3][0]=0;
    wash_count[3][1]=0;
    wash_count[3][2]=0;    
}

void main(void)
{
// 지역변수 선언
char str1[20];
char str2[20];
char mode_index;
char motor[3] = {175, 100, 225};  // 세탁기 속도 : 모터 세탁(70%), 헹굼(40%), 탈수(90%)
int i;

// Port 설정
DDRA=PORTA=0xff;// LED 출력
DDRB=PORTB=0xff;// 모터 출력
DDRC=PORTC=0xff;// LCD 출력
DDRD=0x00;      // PORTD를 입력으로 사용 (switch)
PIND=0x00;
DDRF=PORTF=0x00;

// ADC변환
ADMUX=0x20;
ADCSRA=0xa7;
ADCSRA |= 0x40;     // ADC start conversion
ADCSRA |= 0x08;     // ADC interrupt enable

// 인터럽트
EICRA=0x20;
EIMSK=0x04;
EIFR=0x04;

// 타이머 0번 - 버저
OCR0=0x4b;
TCNT0=0x0006; 

// 타이머 1번 설정 - 시계 타이머
TCCR1A=0x00;
TCCR1B=0x02;

// 타이머 2번 설정 - 모터 속도 제어
TCCR2=0x4b;
OCR2=0x00FA;
TCNT2=0x0006;

// LCD 사용을 위한 I2C설정
twi_master_init(100);   // 100khz 통신 설정
lcd_twi_init(0x27, 16); // LCD주소, 출력 bit수 설정
#asm("sei")

while (1)
      {        
        while(WashingMachine_ON)
        {
            if(WM_flag)
            {
                lcd_gotoxy(0,0);
                lcd_puts(alarm[0]);
                delay_ms(300);
            }
            
            // 모드 선택
            while(WM_flag)
            {
                lcd_gotoxy(0,1);
                 
                if(adc_data < 63)
                {
                    mode_index=0;   // Standard Mode
                    selfmode_flag = 0;
                }
                else if(adc_data < 127)
                {
                    mode_index=1;   // Power Mode
                    selfmode_flag = 0;
                }
                else if(adc_data < 191)
                {
                    mode_index=2;   // Speed Mode
                    selfmode_flag = 0;
                }
                else
                {
                    mode_index=3;   // Self Mode
                    selfmode_flag = 1;
                }
                lcd_puts(mode[mode_index]);
                modesel_flag = 1;  
            }
            
            if(!WM_flag)
            {
                lcd_gotoxy(0,0);
                lcd_puts("mode select!");
                delay_ms(1000);
            }
            
            // 셀프 모드 설정
            while(selfmode_flag)
            {   
                if(!SW_wash)
                {                     // 스위치 확인
                    if(!PushFlg)
                    {             
                        PushFlg=1;
                        wash_count[3][0]++;
                        delay_ms(100);
                    };
                }
                else    PushFlg=0;
                
                if(!SW_rinse)
                {                     
                    if(!PushFlg)
                    {             
                        PushFlg=1;            
                        wash_count[3][1]++;
                        delay_ms(100);
                    };
                }
                else    PushFlg=0;
                
                if(!SW_dry)
                {                     
                    if(!PushFlg)
                    {             
                        PushFlg=1;             
                        wash_count[3][2]++;
                        delay_ms(100);
                    };
                }
                else    PushFlg=0;
                
                sprintf(str1, "Count - wash: %d", wash_count[3][0]);
                sprintf(str2, "rinse:%d, dry:%d", wash_count[3][1], wash_count[3][2]);
                
                lcd_gotoxy(0,0);
                lcd_puts(str1);
                lcd_gotoxy(0,1);
                lcd_puts(str2);
                washcnt_flag = 1;
            }
            
            while(Start)
            {
                if(Start_flag)
                {
                    lcd_clear();
                    Start_flag = 0;
                    // 분으로 계산 - 시뮬레이션 동작을 위해 분을 1로 설정
                    time_arr[0] = wash_count[mode_index][0] * 1;
                    time_arr[1] = wash_count[mode_index][1] * 1;
                    time_arr[2] = wash_count[mode_index][2] * 1; 
                }
                
                for( i=0; i < 3; i++)
                {
                    // 시간 설정
                    hour = time_arr[i] / 60;
                    min = time_arr[i] % 60;
                    sec = 0;
                    msec = 0;
                    
                    // 타이머 켜기 
                    TCNT1H=0xB1;
                    TCNT1L=0xE0;// 10msec
                    TCCR2=0x7b;
                    TIMSK=0x04;
                    
                    // 세탁 단계에 따라 모터의 속도 조절
                    OCR2 = motor[i];

                    while(hour != 0 || min != 0 || sec !=0)
                    {
                        PORTA = LED_run;        // 동작 중 LED - 빨간색
                        while(pause)
                        {
                            lcd_gotoxy(0,0);
                            lcd_puts(alarm[4]);
                            PORTA = LED_pause;  // 일시정지 LED : 노란색         
                        }

                        lcd_gotoxy(0,0);
                        lcd_puts(alarm[i+1]);
                        sprintf(str2, "T: %02d:%02d:%02d", hour, min,sec);
                        lcd_gotoxy(0,1);
                        lcd_puts(str2);  
                    }
                    PORTA = LED_end;            // 한 모드가 끝났을 때 신호 - LED : 초록색
                    TCCR2=0x4b;
                    delay_ms(100);
                }
                // 모드 종료를 위한 초기화
                wm_init();
                
                // 종료 알림 - LCD
                lcd_gotoxy(0,0);
                lcd_puts(alarm[5]);
                delay_ms(3000);
                lcd_clear();
                
                // 버저
                for(i=0; i<3; i++)
                {
                    buzzer(480, 24);
                    buzzer(320, 16);
                    delay_ms(1000);
                }
                delay_ms(2000);
            }
        }
      }
}
