#define SERIAL_SPEED 9600

#define INPUT_PIN 2 // пин выключателя или радиореле
#define TRIGGER_LED 3 // светодиод для выключателя
#define IN_220_PIN 4 // пин реле 220в
#define ON_PIN 7 // пин зажигания
#define STOP_PIN 8 // пин остановки
#define ERROR_LED 9 // пин светодиода ошибки
#define A 10    //пины для управления обмотками (А-крайний левый контакт)
#define B 11
#define C 12
#define D 13

#define ON_TIMEOUT 5000 // время работы стартера
#define WAIT_AFTER_START_TIMEOUT 5000 // перерыв между попытками запуска
#define MAX_RETRYES 20 // максимальное количество попыток запуска перед сваливанием в ошибку двигателя
#define STAGE_DELAY 500 // задержка между переключением стадий
#define MAIN_LOOP_TIMEOUT 10 // задержка главного цикла
#define STOP_TIMEOUT 5000 // задержка нажатия остановки
#define FULL_CYCLE_TIMEOUT 10000 // задержка между циклом остановка - запуск
#define WORK_BEFORE_STOP_ENABLE 1 // задержка перед тем, как будет возможно выключить двигатель с реле
#define TRIGGER_DELAY 3000

#define CHOKE_ON 43
#define CHOKE_OFF 43

// задержка между шагами, для моторедуктора 2170 максимум это примерно 4мс (скорость штока 10 мм/сек)
// для замедления - увеличить, ускорять не рекомендуется
int delStep = 4;

enum S
{ 
	IDLE, 			// idle
					//		-> IDLE if INPUT_PIN is LOW
					//		-> if FULL_CYCLE_TIMEOUT, MOVE_TR if INPUT_PIN is HIGH
	MOVE_CHOKE, 	// move air trigger (podsos)
					//		-> IDLE if INPUT_PIN is LOW or IN_220_PIN is HIGH
					//		-> move choke, then START_ENGINE0 if INPUT_PIN is HIGH
	START_ENGINE0, 	// enable ignition
					//		-> STOP0 if INPUT_PIN is LOW or IN_220_PIN is HIGH
					// 		-> ENGINE_ERROR, if counter>=MAX_RETRYES
					//		-> enable ignition, counter++, then START_ENGINE1 if INPUT_PIN is HIGH
	START_ENGINE1, 	// wait for 220 while ON_TIMEOUT is not reached
					//		-> disable ignition then STOP0 if INPUT_PIN is LOW
					//		-> wait if IN_220_PIN is LOW and if INPUT_PIN is HIGH
					//		-> if timeout or IN_220_PIN is HIGH then START_ENGINE2 if INPUT_PIN is HIGH
	START_ENGINE2, 	// disable ignition
					//		-> disable ignition then STOP0 if INPUT_PIN is LOW
					//		-> disable ignition then CHOKE_BACK, if IN_220_PIN is HIGH
					//		-> disable ignition then WAITING, if IN_220_PIN is LOW and if INPUT_PIN is HIGH
	WAITING, 		// waiting
					//		-> IDLE if INPUT_PIN is LOW
					// 		-> CHOKE_BACK if IN_220_PIN is HIGH and if INPUT_PIN is HIGH
					//		-> START_ENGINE0 if timeout or IN_220_PIN is LOW and if INPUT_PIN is HIGH
	CHOKE_BACK,		// move choke back (IN_220_PIN is HIGH according previous step)
					//		-> IDLE if INPUT_PIN is LOW
					//		-> move choke, then WORKING if INPUT_PIN is HIGH
	WORKING, 		// working
					//		-> IDLE if if INPUT_PIN becomes HIGH
					//		-> MOVE_CHOKE if IN_220_PIN is LOW
	STOP0, 			// stop engine
					//		-> disable choke and ignition if need, fullCycleCounter=millis(), then IDLE, if IN_220_PIN is LOW
					// 		-> disable choke and ignition if need, fullCycleCounter=millis(), then enable STOP_PIN, then STOP1, if IN_220_PIN is HIGH
	STOP1,			// stage 2 of stop
					//		-> if stop timeout then disable STOP_PIN then IDLE
	ENGINE_ERROR, 	// power on error led
	//All stages switching in loop with delay STAGE_DELAY ms
};

unsigned short stage = S::IDLE;
bool mainTrigger = false;
bool inputPowerTrigger = false;
bool chokeState = false;
bool ignitionState = false;
bool stopState = false;
unsigned long lastTime = 0;
unsigned long startCounter = 0;
unsigned long fullCycleCounter = 0;
unsigned long startTime = 0;
unsigned long delayTime = 0;
unsigned long workTime = 0;
unsigned long stopTime = 0;
bool firstRun = true;
unsigned long triggerTime = 0;

void checkMainPin()
{
  unsigned long foo = millis();
	if (digitalRead(INPUT_PIN) == 1)
    if (Abs(foo - triggerTime)>=TRIGGER_DELAY)
    {
		  mainTrigger = !mainTrigger;
      triggerTime = foo;
      if (mainTrigger)
        digitalWrite(TRIGGER_LED, HIGH);
      else
        digitalWrite(TRIGGER_LED, LOW);
    }
}

void checkInput220Pin()
{
	if (digitalRead(IN_220_PIN) == 1)
  {
		inputPowerTrigger = true;
    //Serial.println("Power trigger = true");
  }
	else
  {
		inputPowerTrigger = false;
    //Serial.println("Power trigger = false");
  }
}

void moveChoke()
{
	//digitalWrite(CHOKE_PIN1, HIGH);
  for (int i = 0; i < CHOKE_ON; i++) rotateR();  // вверх на 20мм
  stopStep();
	chokeState = true;
}

void chokeBack()
{
	//digitalWrite(CHOKE_PIN2, HIGH);
  for (int i = 0; i < CHOKE_OFF; i++) rotateL();  // вверх на 20мм
  stopStep();
	chokeState = false;
}

void chokeBack2()
{
  //digitalWrite(CHOKE_PIN2, HIGH);
  for (int i = 0; i < 20; i++) rotateL();  // вверх на 20мм
  stopStep();
  chokeState = false;
}


void enableIgnition()
{
	digitalWrite(ON_PIN, HIGH);
	ignitionState = true;
}

void disableIgnition()
{
	digitalWrite(ON_PIN, LOW);
	ignitionState = false;
}

void enableStop()
{
	digitalWrite(STOP_PIN, HIGH);
	stopState = true;
}

void disableStop()
{
	digitalWrite(STOP_PIN, LOW);
	stopState = false;
}

void enableError()
{
	digitalWrite(ERROR_LED, HIGH);
}

void disableError()
{
  digitalWrite(ERROR_LED, LOW);
}

// +++++ ОСНОВНЫЕ ФУНКЦИИ +++++

void stopStep() { // выключение двигателя
  Step(0, 0, 0, 0);
}

void rotateR() // 24 шага по часовой стрелке = 1 оборот оси = 1 мм движения штока вниз
{
  for (byte i = 0; i < 6; i++) { // один такой блок сдвигает шток на 1/6 мм
    //Step(1, 0, 1, 0);            // один такой шаг сдвигает шток на 1/24 мм
    //Step(0, 1, 1, 0);
    //Step(0, 1, 0, 1);
    //Step(1, 0, 0, 1);
    Step(1, 0, 0, 0);            // один такой шаг сдвигает шток на 1/24 мм
    Step(0, 0, 1, 0);
    Step(0, 1, 0, 0);
    Step(0, 0, 0, 1);
  }
}

void rotateL() // 24 шага по часовой стрелке = 1 оборота оси = 1 мм движения штока вверх
{
  for (byte i = 0; i < 6; i++) { // один такой блок сдвигает шток на 1/6 мм
    //Step(1, 0, 0, 1);            // один такой шаг сдвигает шток на 1/24 мм
    //Step(0, 1, 0, 1);
    //Step(0, 1, 1, 0);
    //Step(1, 0, 1, 0);
    
    Step(1, 0, 0, 0);            // один такой шаг сдвигает шток на 1/24 мм
    Step(0, 0, 0, 1);
    Step(0, 1, 0, 0);
    Step(0, 0, 1, 0);
  }
}

void Step(boolean a, boolean b, boolean c, boolean d) // один шаг
{
  digitalWrite(A, a);
  digitalWrite(B, b);
  digitalWrite(C, c);
  digitalWrite(D, d);
  delay(delStep);
}

void StepH(boolean a, boolean b, boolean c, boolean d) // один шаг
{
  digitalWrite(A, a);
  digitalWrite(B, b);
  digitalWrite(C, c);
  digitalWrite(D, d);
  delay(delStep / 2);
}

void setup() {
  pinMode(STOP_PIN, OUTPUT);
  pinMode(ON_PIN, OUTPUT);
  pinMode(ERROR_LED, OUTPUT);
  pinMode(TRIGGER_LED, OUTPUT);
  pinMode(INPUT_PIN, INPUT);
  pinMode(IN_220_PIN, INPUT);
  Serial.begin(SERIAL_SPEED);

  pinMode(A, OUTPUT);
  pinMode(B, OUTPUT);
  pinMode(C, OUTPUT);
  pinMode(D, OUTPUT);
  
  fullCycleCounter = millis();

  //chokeBack2();

  Serial.println("Idle");
}

unsigned long Abs(unsigned long v)
{
	unsigned long t = abs(v);
	return t;
}

void loop() {
	checkMainPin();
	checkInput220Pin();

  unsigned long curTime = millis();
	if (Abs(curTime - lastTime) >= STAGE_DELAY)
	{
		switch (stage)
		{
			case S::IDLE: 			// idle
				//		-> IDLE if INPUT_PIN is LOW
				//		-> if FULL_CYCLE_TIMEOUT, MOVE_TR if INPUT_PIN is HIGH
        //Serial.println("Idle");
				if (mainTrigger && ((Abs(curTime-fullCycleCounter)>=FULL_CYCLE_TIMEOUT)|| firstRun))
        {
          firstRun = false;
					stage = S::MOVE_CHOKE;
          Serial.println("Move choke!");
        }
				break;
			case S::MOVE_CHOKE: 	// move air trigger (podsos)
					//		-> IDLE if INPUT_PIN is LOW or IN_220_PIN is HIGH
					//		-> move choke, then START_ENGINE0 if INPUT_PIN is HIGH
        //Serial.println("Move choke");
				if (!mainTrigger)
        {
					stage = S::IDLE;
          Serial.println("Idle");
        }
				else if (mainTrigger)
				{
					moveChoke();
					stage = S::START_ENGINE0;
          Serial.println("Starting engine phase 1");         
				}
				break;
			case S::START_ENGINE0: 	// enable ignition
				// 		-> ENGINE_ERROR, if counter>=MAX_RETRYES
				//		-> STOP0 if INPUT_PIN is LOW or IN_220_PIN is HIGH
				//		-> enable ignition, counter++, then START_ENGINE1 if INPUT_PIN is HIGH
        disableError();
       // Serial.println("Starting engine phase 1");
				if (startCounter>=MAX_RETRYES)
        {
					stage = S::ENGINE_ERROR;
          Serial.println("Engine error!");         
        }
				else if (!mainTrigger || inputPowerTrigger)
		    {
					stage = S::STOP0;
         Serial.println("Engine stop!");         
		    }
				else if (mainTrigger && !inputPowerTrigger)
				{
					enableIgnition();
					startCounter++;
					stage = S::START_ENGINE1;	
					startTime = curTime;
          Serial.println("Starting engine phase 2");         
				}
				break;
			case S::START_ENGINE1: 	// wait for 220 while ON_TIMEOUT is not reached
				//		-> disable ignition then STOP0 if INPUT_PIN is LOW
				//		-> wait if IN_220_PIN is LOW and if INPUT_PIN is HIGH
				//		-> if timeout or IN_220_PIN is HIGH then START_ENGINE2 if INPUT_PIN is HIGH
        //Serial.println("Starting engine phase 2");
				if (!mainTrigger)
	      {
					stage = S::STOP0;
          Serial.println("Engine stop!");         
	      }
				else if (inputPowerTrigger || (Abs(curTime-startTime)>=ON_TIMEOUT))
		    {
					stage = S::START_ENGINE2;
          Serial.println("Starting engine phase 3");         
		    }
				break;
			case S::START_ENGINE2: 	// disable ignition
					//		-> disable ignition then STOP0 if INPUT_PIN is LOW
					//		-> disable ignition then CHOKE_BACK, if IN_220_PIN is HIGH
					//		-> disable ignition then WAITING, if IN_220_PIN is LOW and if INPUT_PIN is HIG
          //Serial.println("Starting engine phase 3");
					disableIgnition();
					if (!mainTrigger)
         {
						stage = S::STOP0;
            Serial.println("Engine stop!");         
         }
					else if (inputPowerTrigger)
         {
						stage = S::CHOKE_BACK;
            Serial.println("Moving choke back!");         
         }
					else
					{
						stage = S::WAITING;
						delayTime = curTime;
            Serial.println("Waiting for start");         
					}
				break;
			case S::WAITING: 		// waiting
					//		-> IDLE if INPUT_PIN is LOW
					// 		-> CHOKE_BACK if IN_220_PIN is HIGH and if INPUT_PIN is HIGH
					//		-> START_ENGINE0 if timeout or IN_220_PIN is LOW and if INPUT_PIN is HIGH
          //Serial.println("Waiting for start");
					if (!mainTrigger)
	        {
						stage = S::STOP0;
            Serial.println("Engine stop!");         
	        }
					else if (inputPowerTrigger)
		      {
						stage = S::CHOKE_BACK;
            Serial.println("Move choke back");         
		      }
					else if (Abs(curTime - delayTime)>=WAIT_AFTER_START_TIMEOUT)
		      {
						stage = S::START_ENGINE0;
            Serial.println("Starting engine phase 1");        
		      }
				break;
			case S::CHOKE_BACK:		// move choke back (IN_220_PIN is HIGH according previous step)
					//		-> IDLE if INPUT_PIN is LOW
					//		-> move choke, then WORKING if INPUT_PIN is HIG
          //Serial.println("Move choke back");
					chokeBack();
					if (!mainTrigger)
		      {
						stage = S::IDLE;
            Serial.println("Idle");         
		      }
					else
					{
						workTime = curTime;
						stage = S::WORKING;
            Serial.println("Working!");         
					}
				break;
			case S::WORKING: 		// working
					//		-> startCounter = 0, STOP0 if tumeout and if INPUT_PIN is HIGH
					//		-> startCounter = 0, MOVE_CHOKE if IN_220_PIN is LOW and INPUT_PIN is LOW
          //Serial.println("Working");
					startCounter = 0;
          disableError();
					if (!mainTrigger && (Abs(curTime - workTime)>=WORK_BEFORE_STOP_ENABLE))
		      {
						stage = S::STOP0;
            Serial.println("Engine stop!");         
		      }
          else if (mainTrigger && !inputPowerTrigger) // engine stopped
		      {
						stage = S::MOVE_CHOKE;
            Serial.println("Moving choke!");         
		      }
				break;
			case S::STOP0:			// stop engine
					//		-> disable choke and ignition if need, fullCycleCounter=millis(), then IDLE, if IN_220_PIN is LOW
					// 		-> disable choke and ignition if need, fullCycleCounter=millis(), then enable STOP_PIN, then STOP1, if IN_220_PIN is HIGH
          //Serial.println("Stop phase 1");
					disableIgnition();
          Serial.println("Move choke back if needed");         
					chokeBack();
					fullCycleCounter=curTime;
					//if (!inputPowerTrigger)
					stage = S::IDLE;
          startCounter = 0;
					//else 
					//{
						//stopTime = curTime;
          Serial.println("Stop engine begin");         
					enableStop();
					//stage = S::STOP1;
          delay(STOP_TIMEOUT);
          disableStop();
          Serial.println("Stop engine end");         
          //stage = S::IDLE;
          Serial.println("Idle");         
					//}
				break;
			/*case S::STOP1:			// stage 2 of stop
        Serial.println("Stop phase 2");
				//		-> if stop timeout then disable STOP_PIN then IDLE
				if (Abs(curTime - stopTime)>=STOP_TIMEOUT)
        {
          disableStop();
					stage = S::IDLE;
        }
				break;*/
			case S::ENGINE_ERROR: 	// power on error led
        //Serial.println("Engine error");
				enableError();
        stage = S::STOP0;
        Serial.println("Engine stop");         
        mainTrigger = false;
        digitalWrite(TRIGGER_LED, LOW);
				break;
		}
	}
	delay(MAIN_LOOP_TIMEOUT);
}
