//Tetris 1.0
#include <Arduino.h>
#include <U8g2lib.h>
#include <IRrecv.h>

#define MAX_X 10 // 横向方块数
#define MAX_Y 21 // 纵向方块数
#define cubelength 6 // 边长
#define birthX 4 //出生位置x
#define birthY 0 //出生位置y

// 当前状态
#define SYS_READY 0 // 就绪
#define SYS_PAUSE 1 // 暂停
#define SYS_PLAYING 2 // 正常玩
#define SYS_GAME_OVER 3 // 结束
bool needFresh = false; // 需要刷新屏幕
int SYS_STATE = SYS_READY; // 当前系统状态
char World[MAX_X][MAX_Y];// 静态世界，所有不再移动的方块
char MovingWorld[MAX_X][MAX_Y];// 移动的世界，当前正在下落的方块
char FutureWorld[MAX_X][MAX_Y];// 未来的世界，用来预测 砖块移动后，是否会碰撞
uint64_t lastFallTime=0;// 下落计时器，上次下落时间
int fallSpeed=3;// 下落速度

bool buttomWait = false; // 方块到底, 等待一个循环，期间图形还可以旋转或者平移，如果下一循环钱没操作吗，则动态的图形将被合并到静态世界中
int bagArr[7]; // 方块随机结果储存数组
int bagSize = 0; // 数组当前使用容量
int bagIdx = 0; // 当前读取下标
int score = 0; // 游戏得分

// 方块类型
#define SHP_I 0

// 方块定义，4个值，对应方块旋转后的4x4坐标图型
uint16_t SHAPE_ARR[7][4] = {
  {0x0F00,0x4444,0x0F00,0x4444}, // SHP_I
  {0x8E00,0x44C0,0xE200,0xC880}, // SHP_J
  {0x2E00,0xC440,0xE800,0x88C0}, // SHP_L
  {0x6600,0x6600,0x6600,0x6600}, // SHP_O
  {0x6C00,0x8C40,0x6C00,0x8C40}, // SHP_S
  {0x4E00,0x4C40,0x0E40,0x4640}, // SHP_T
  {0xC600,0x4C80,0xC600,0x4C80} // SHP_Z
};

typedef struct {
  int direction=0;   // 旋转方向
  int x; // 横坐标
  int y; // 纵坐标
  int type; // 类型
} shape_obj;  // 命令记录

shape_obj MOVING_SHAPE;  // 当前方块

///-----------------------------红外相关------------------------------------------------
#define VCC_PIN 13 // 使用13针脚拉高电平为红外接收器供电
#define RECV_PIN 14 // 接收红外针脚      D7
IRrecv irrecv(RECV_PIN);// 配置接收针脚
decode_results results;// 接收数据暂存
uint64_t last_ir_code = 0xCD123456;// 上一次的红外指令
uint64_t last_ir_time=0;// 计时器，上次时间
///-----------------------------显示相关------------------------------------------------
int startx = 2; // 显示起始坐标
int starty = 0; // 显示起始坐标

// 显示屏 ssd1306驱动  分辨率  未知厂商 整屏刷新 硬件I2c   纵向屏幕
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R3, /* clock=*/ SCL, /* data=*/ SDA, /* reset=*/ U8X8_PIN_NONE);  

// 主循环，游戏逻辑和显示刷新
TaskHandle_t Task0;
void mainTask(void *pvParameters){
  for(;;){
    if(SYS_STATE == SYS_PLAYING){
      fallTicker(); // 下落计时器
    }
    doFresh(); // 屏幕刷新器
    if(SYS_STATE == SYS_PLAYING){
      delay(5);
    }else{
      delay(300);
    }    
  }
}

// 控制循环。红外遥控指令处理
TaskHandle_t Task1;
void irrcTask(void *pvParm){
  for(;;){
      
    // 收到信号
    if(irrecv.decode(&results)){    

      // 只解析nec
      if(results.decode_type == NEC){

        if(results.repeat && (millis() - last_ir_time) < 150){
          irrecv.resume();
          continue;
        }

        if(!results.repeat){
          last_ir_code = results.value; // 暂存最新指令
        }

        if(SYS_STATE == SYS_PLAYING){
          if(last_ir_code == 3442635660 || last_ir_code == 3442627500){       // ok up     
            turnShape();
            buttomWait = false; // 旋转可能导致本来到底了的图形又悬空了，要取消到底等待的标志，留给下一循环继续判断下坠逻辑    
          }else if(last_ir_code == 3442625460){      //down
            fall(1);
            delay(55);
            fall(1);
            delay(55);
            fall(1);
          }else if(last_ir_code ==3442622910){ // 遥控本地键，重新开始游戏 
            init();
          }else {
            if(last_ir_code == 3442645350){ // left
              moveShapeLeftOrRight(-1);
            }
            if(last_ir_code == 3442639740){ // right
              moveShapeLeftOrRight(1);
            }
            buttomWait = false;// 平移可能导致本来到底了的图形又悬空了，要取消到底等待的标志，留给下一循环继续判断下坠逻辑 
          }
          last_ir_time = millis();      
        }
        
        if(last_ir_code == 3442649430){ // play/pause
          
            if(SYS_STATE == SYS_PLAYING){ //暂停游戏
              SYS_STATE = SYS_PAUSE;
            }else if(SYS_STATE == SYS_READY){ // 开始游戏
              SYS_STATE = SYS_PLAYING;
              randomGenerateShape();
            }else if(SYS_STATE == SYS_PAUSE){ // 继续游戏
              SYS_STATE = SYS_PLAYING;
            }else if(SYS_STATE == SYS_GAME_OVER){ //游戏结束
              init();
            }

            Serial.println(SYS_STATE);
            delay(400);// 避免连发指令干扰操作
          }        
      }
      
      irrecv.resume();
    }
  }
}
// 方块随机算法，给出一组7个随即顺序的不同类型的方块
void bag7(){  
  bagSize =0;
  bagIdx = 0; 

  int x = 0;
  while(bagSize<6){
    int type = random(6);
    bool isok = true;
    for(int i=0; i< bagSize;i++){
      // 舍弃重复数字
      if(bagArr[i] == type){
        isok = false;
      }
    }
    if(isok){
      bagArr[x] = type;
      x++;
      bagSize++;
    }
  }  
}

// 随机生成方块
void randomGenerateShape(){

  int type = bagArr[bagIdx]; // 获取随机方块类型
  if(bagIdx<6){
    bagIdx++; // 移动下标
  }else{
    bag7(); // 生成下一组随机方块
  }

  shape_obj s;
  if(type == SHP_I){
    s.x = birthX-1; // 长条左移一格
  }else{
    s.x = birthX;
  } 
  s.y = birthY;
  s.type = type;
  MOVING_SHAPE = s;

  freshMovingShapeInMovingWorld(); // 将动态方块，刷新到动态世界中

  if(!seeFutureAndMakeItTrue(s)){    
    SYS_STATE = SYS_GAME_OVER;// 游戏结束
    freshScreen();
  }  
}

// 得到一个复制的动态方块对象，用于未来检测
shape_obj copyMovingShape(){
  shape_obj nextPosObj;
  nextPosObj.type = MOVING_SHAPE.type;
  nextPosObj.x = MOVING_SHAPE.x;
  nextPosObj.y = MOVING_SHAPE.y;
  nextPosObj.direction = MOVING_SHAPE.direction;
  return nextPosObj;
}

// 旋转方块
void turnShape(){
  shape_obj nextPosObj = copyMovingShape();
  if(nextPosObj.direction < 3){
    nextPosObj.direction++;
  }else{
    nextPosObj.direction=0;
  }
  seeFutureAndMakeItTrue(nextPosObj);
}

// 平移方块
void moveShapeLeftOrRight(int direction){
  shape_obj nextPosObj = copyMovingShape();
  nextPosObj.x = nextPosObj.x + direction;
  seeFutureAndMakeItTrue(nextPosObj);
}

// 下落方块
void fall(int step){
  shape_obj nextPosObj = copyMovingShape();
  nextPosObj.y = nextPosObj.y+step;

  // 到底了，等待一个循环
  if(!seeFutureAndMakeItTrue(nextPosObj)){
    buttomWait = true;
  }
}

// 绘制图像到动态世界
void freshMovingShapeInMovingWorld(){

  // 清空
  for(int y=0;y<MAX_Y;y++){
    for(int x=0; x< MAX_X; x++){
      MovingWorld[x][y]=0;
    }
  }

  // 将形状code，按位，转化为形状坐标的二维数组，并放置在动态世界中
  for(int i=0;i<16;i++){
    unsigned int flag = (SHAPE_ARR[MOVING_SHAPE.type][MOVING_SHAPE.direction]>>(15-i)) & 0x000000000000001;
    int sx = MOVING_SHAPE.x + i%4;
    int sy = MOVING_SHAPE.y + i/4;
    if(sx < MAX_X && sy < MAX_Y){
      MovingWorld[MOVING_SHAPE.x + i%4 ][MOVING_SHAPE.y + i/4]=flag;
    }
  }
  freshScreen();
}

// 遇见未来,如果可能，实现它
bool seeFutureAndMakeItTrue(shape_obj nextPosObj){

  // 清空
  for(int y=0;y<MAX_Y;y++){
    for(int x=0; x< MAX_X; x++){
      FutureWorld[x][y]=0;
    }
  }

  // 图形有几个方块
  int oldFutureBoxNum = 0;

  // 将形状code，按位，转化为形状坐标的二维数组，并放置在未来世界中
  for(int i=0;i<16;i++){
    // 0110110000000000
    unsigned int flag = (SHAPE_ARR[nextPosObj.type][nextPosObj.direction]>>(15-i)) & 0x000000000000001;
    
    int sx = nextPosObj.x + i%4;
    int sy = nextPosObj.y + i/4;
    if(sx < MAX_X && sy < MAX_Y && sx >= 0 && sy>=0){
      FutureWorld[nextPosObj.x + i%4 ][nextPosObj.y + i/4]=flag;

      // 计数
      if(flag==1){
        oldFutureBoxNum++;
      }
    }
  }

  // 静态世界有几个方块
  int worldBoxNum = 0;
  for(int y=0;y<MAX_Y;y++){
    for(int x=0; x< MAX_X; x++){
      if(World[x][y]==1){
        worldBoxNum++;
      }
    }
  }

  // 动态世界有几个方块
  int movingWorldBoxNum = 0;
  for(int y=0;y<MAX_Y;y++){
    for(int x=0; x< MAX_X; x++){
      if(MovingWorld[x][y]==1){
        movingWorldBoxNum++;
      }
    }
  }

  // 合并静态世界和未来世界
  for(int y=0;y<MAX_Y;y++){
    for(int x=0; x< MAX_X; x++){
    
      // 在预测世界，加上静态的方块
      if(World[x][y]==1){
        FutureWorld[x][y]=1;
      }
    } 
  }

  // 合并后的方块数量
  int uionWorldBoxNum = 0;
  for(int y=0;y<MAX_Y;y++){
    for(int x=0; x< MAX_X; x++){
      if(FutureWorld[x][y]==1){
        uionWorldBoxNum++;
      }
    }
  }

  // 未来世界的方块比当前动态静态世界的方块数量少，说明图形碰撞或者超出了游戏边界，返回false
  bool rs = (movingWorldBoxNum + worldBoxNum == uionWorldBoxNum);
  if(rs){
    MOVING_SHAPE = nextPosObj;
    freshMovingShapeInMovingWorld();
  }
  return rs;
}

// 下坠计时器
void fallTicker(){
  uint64_t fallDuration = 1000 - fallSpeed*50; // 下落间隔时间

  // 到时间啊
  if((millis()-lastFallTime) >= fallDuration){

    if(!buttomWait){
      fall(1);      
    }else{
      
      // 先将旧方块，合并到静态世界
      for(int y=0;y<MAX_Y;y++){
        for(int x=0; x< MAX_X; x++){        
          if(MovingWorld[x][y]){
            World[x][y]=1;
          }
        } 
      }

      // 有形状下落完成，尝试消行；
      cleanBox();

      // 生成新砖块
      randomGenerateShape();
      buttomWait = false;
    }
    lastFallTime = millis();
  }
}

// 消行
void cleanBox(){

  // 从底部向上逐行扫描
  for(int y=(MAX_Y-1);y>=0;){
    bool full = true;
    for(int x=0; x< MAX_X; x++){
       
      if(!World[x][y]){
        full = false;
      }
    }

    // 消行
    if(full){      
      
      // 上面的所有行，整体下移
      for(int ny=y;ny>=0;ny--){
        if(ny!=0){
          for(int x=0; x< MAX_X; x++){
            World[x][ny]=World[x][ny-1];
          }
        }else{
          for(int x=0; x< MAX_X; x++){
            World[x][ny]=0;
          }
        }      
      }

      score++;
      if(score % 10 == 0){
        fallSpeed++; // 每得10分难度增加
      }
    }else{
      y--; // 向上扫描一行
    }
  }
}

// 初始化
void init(){
  for(int x=0; x< MAX_X; x++){
    for(int y=0;y<MAX_Y;y++){
      World[x][y]=0;
      MovingWorld[x][y]=0;
    }
  }

  lastFallTime = millis();
  SYS_STATE = SYS_READY;
  score = 0;
  fallSpeed = 5;
  bag7();
  freshScreen();
}

void freshScreen() {
  needFresh = true;// 为了防止每次循环都刷新屏幕，设计需要刷新的标志位
}

void doFresh() {
  if(!needFresh){ // 为了防止每次循环都刷新屏幕，设计需要刷新的标志位
    return;
  }
  u8g2.clearBuffer(); 
  if(SYS_STATE == SYS_READY){
    u8g2.setFont(u8g2_font_t0_11b_tf);
    u8g2.setCursor(8,60);
    u8g2.print("TETRIS");
    
  }else if(SYS_STATE != SYS_GAME_OVER){
    u8g2.drawFrame(startx-2, starty, 64-startx + 1, 128-starty);
    for(int x=0; x< MAX_X; x++){    
      for(int y=0;y<MAX_Y;y++){
        int drawX = startx + x*cubelength;
        int drawY = starty + y*cubelength;
        if(World[x][y]){
          u8g2.drawFrame(drawX, drawY, cubelength-1, cubelength-1);
        }
        if(MovingWorld[x][y]){
          u8g2.drawFrame(drawX, drawY, cubelength-1, cubelength-1);
        }
      }
    }
  }else{
    u8g2.setFont(u8g2_font_t0_11b_tf);
    u8g2.setCursor(0,55);
    u8g2.print("GAME OVER");
    u8g2.setCursor(0,70);
    u8g2.print("SCORE:");
    u8g2.print(score);
  }
  u8g2.sendBuffer();
  needFresh = false;
}

void setup() {
  pinMode(RECV_PIN, INPUT); // 红外接收数据针脚
  pinMode(VCC_PIN, OUTPUT); // 红外接收器供电
  digitalWrite(VCC_PIN, HIGH);// 使用一个针脚拉高电平，为红外接收器供电  
  irrecv.enableIRIn();// 红外接收初始化
  
  Serial.begin(115200);// 开启串口连接
  while(!Serial)
  delay(50);

  u8g2.setBusClock(1000000);//提高总线频率
  u8g2.begin(); //显示屏初始化
  u8g2.enableUTF8Print();// 允许打印字符
  
  init();// 游戏初始化

  xTaskCreatePinnedToCore(mainTask, "mainTask", 10000, NULL, 1, &Task0, 0); // 大核运行主线程
  xTaskCreatePinnedToCore(irrcTask, "irrcTask", 10000, NULL, 1, &Task1, 1); // 小核处理红外遥控指令
  Serial.println("");
  Serial.println("started!");
}

void loop() {
  // put your main code here, to run repeatedly:
}