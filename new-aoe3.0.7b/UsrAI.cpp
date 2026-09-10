#include "UsrAI.h"
#include<set>
#include<unordered_map>
#include<list>
#include <cstdlib>
#include <cmath>

using namespace std;
tagGame tagUsrGame;
ins UsrIns;
/*##########DO NOT MODIFY THE CODE ABOVE##########*/

/* =====================================================================
 *  我的AI代码
 *  思路:
 *  1. 用全局变量记录村民现在在干什么活(用一张表:村民编号 -> 工种)
 *  2. 每帧先获取游戏信息,然后:
 *      - 让市镇中心生产村民、升级时代
 *      - 安排建筑(房子、市场、兵营、靶场、马厩、农田、箭塔、学院)
 *      - 研究科技
 *      - 给空闲的村民安排工作
 *      - 生产士兵(前期少量保底,铜器成型,反攻养满)
 *      - 探路(村民找金矿,侦察骑兵找敌方基地)
 *      - 防守(敌人靠近就收拢村民,箭塔和士兵守家)
 *      - 指挥军队打架
 *  3. 游戏分四个阶段:
 *      阶段1:游戏开始,探路和采集资源(第一波之前)
 *      阶段2:防御第一波攻击,升级铜器(第一波到来~铜器升级完成)
 *      阶段3:防御第二、三波攻击,规模化造兵
 *      阶段4:进攻敌人基地,完成胜利目标
 * ===================================================================== */

/* ---------- 工种编号(村民现在在干什么) ---------- */
#define WORK_NONE    0   //没有工作
#define WORK_BERRY   1   //采浆果
#define WORK_WOOD    2   //砍树
#define WORK_STONE   3   //挖石头
#define WORK_HUNT    4   //打猎(打瞪羚)
#define WORK_GOLD    5   //挖金矿
#define WORK_FARM    6   //种田
#define WORK_BUILD   7   //盖房子(专门派一个村民盖房子)

//第一波攻击在6000帧左右到来,这里定义"第一波即将到来"的时间点:
//5600帧后停止祭司探路,回安全点,交给保护代码接管(留400帧回程余量)
#define PRIEST_EXPLORE_END_FRAME 5600

//探路遇敌后撤帧数(25fps,75帧=3秒,足够拉开距离后切换下一目标)
#define PRIEST_RETREAT_FRAMES 75

/* ---------- 全局变量(跨帧保存的) ---------- */
static int gameStage = 1;                 //当前游戏阶段,1游戏开始探路采集,2防御第一波升级铜器,3防御第二三波造兵,4反攻
static int townX = -1;                    //市镇中心的块坐标X
static int townY = -1;                    //市镇中心的块坐标Y

//用来记录村民现在在干什么活的表
static map<int,int> villagerWork;         //村民编号 -> 工种
static map<int,int> lastOrderFrame;       //单位编号 -> 上次给他下指令是哪一帧

//记录科技有没有研究过(研究过了就不用再研究)
static bool hasArrowTech = false;         //谷仓的箭塔科技
static bool hasTowerUpTech = false;       //谷仓的箭塔升级科技(加攻击和射程)
static bool hasWoodTech = false;          //市场的伐木科技
static bool hasWheelTech = false;         //市场的车轮科技
static bool hasGoldTech = false;          //市场的金矿科技
static bool hasFarmTech = false;          //市场的驯养动物科技
static bool hasToolTech = false;          //仓库的工具使用科技
static bool hasInfTech = false;           //仓库的步兵护甲科技
static bool hasAxeTech = false;           //兵营的战斧升级
static bool hasBroadTech = false;         //兵营的阔剑科技
static bool hasCompositeTech = false;     //靶场的复合弓科技
static bool hasAgeUp = false;             //是否已经让市镇中心升级时代了

static int priestLastBlood = -1;          //祭司上一帧血量,受击立刻逃跑用

//祭司探路:固定四点访问(替换旧的之字形探路状态)
static int priestVisitIndex = -1;         // 当前访问第几个目标(-1=未开始,0~3循环访问,直到探路阶段结束)
static int priestVisitOrder[4];            // 访问顺序:存点索引(0~3角点,4=中心),第二近->第三近->最近->中心点(50,50),依次循环
static bool priestVisitSorted = false;     // 是否已完成四点到基地的距离排序
static int priestStillFrames = 0;          // 祭司连续静止帧数(连续10帧不变视为到达)
static double priestLastDR = -1.0;          // 上一帧祭司细节坐标X(到达判定用,细节坐标精度高,避免块坐标粗粒度误判)
static double priestLastUR = -1.0;          // 上一帧祭司细节坐标Y
static bool priestMoveSent = false;         // 当前目标的移动指令是否已发送(每点只发一次)
static int priestRetreatFrames = 0;           // 遇敌后撤剩余帧数(后撤期间不做到达判定,结束后切换下一目标)
static int gameStartFrame = -1;             // 本局游戏开始时的GameFrame(计算相对帧号用,游戏内重启不重置GameFrame)

//地图信息(0是草地,1是海洋,-1是没探索过)
static int gameMap[100][100];

/* =====================================================================
 *  辅助函数
 * ===================================================================== */

//返回一格的长度(把块的坐标换成细节坐标要乘这个)
double UsrAI::blockLength()
{
    return (double)BLOCKSIDELENGTH;
}

//块坐标转细节坐标(取格子的中心)
double UsrAI::detailOf(int block)
{
    double d = (block + 0.5) * blockLength();
    return d;
}

//计算两个块坐标的距离(直线距离)
double UsrAI::distanceBlock(int x1, int y1, int x2, int y2)
{
    int dx = x1 - x2;
    int dy = y1 - y2;
    double d = sqrt((double)(dx * dx + dy * dy));
    return d;
}

//判断某个单位能不能下指令(防止同一帧重复下同一个指令)
//gap是两次指令之间最少隔多少帧
bool UsrAI::canOrder(int sn, int gap)
{
    map<int,int>::iterator it = lastOrderFrame.find(sn);
    if (it == lastOrderFrame.end()) {
        return true;      //从来没下过指令,可以下
    }
    int lastFrame = it->second;
    if (g_frame - lastFrame >= gap) {
        return true;      //距离上次下指令已经够久了,可以下
    }
    return false;         //刚下过指令,再等等
}

//记录某个单位下指令的帧号
void UsrAI::rememberOrder(int sn)
{
    lastOrderFrame[sn] = g_frame;
}

//数一数某种建筑有几个(包括没建完的)
int UsrAI::countBuilding(const tagInfo& info, int buildType)
{
    int num = 0;
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].Type == buildType) {
            num++;
        }
    }
    return num;
}

//找到第一个已经建好的某种建筑的编号,找不到就返回-1
int UsrAI::findBuilding(const tagInfo& info, int buildType)
{
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].Type == buildType && info.buildings[i].Percent >= 100) {
            return info.buildings[i].SN;
        }
    }
    return -1;
}

//数一数某种士兵有几个
int UsrAI::countArmy(const tagInfo& info, int armyType)
{
    int num = 0;
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        if (info.armies[i].Sort == armyType) {
            num++;
        }
    }
    return num;
}

/* =====================================================================
 *  找某类资源对应的存放点
 *  说明:村民采完资源要回存放点上交,选离存放点近的资源,返程路程最短。
 *      浆果→谷仓;木材、石头、金矿、猎物→仓库;
 *      对应建筑都没有→市镇中心(市镇中心能存所有资源)。
 * ===================================================================== */
bool UsrAI::findDropoff(const tagInfo& info, int resType, int& bx, int& by)
{
    //浆果(和农田食物)放谷仓,其他(木/石/金/猎物)放仓库
    int buildType = BUILDING_STOCK;
    if (resType == RESOURCE_BUSH) {
        buildType = BUILDING_GRANARY;
    }
    int dropSN = findBuilding(info, buildType);
    if (dropSN < 0) {
        dropSN = findBuilding(info, BUILDING_CENTER);    //退而求其次,市镇中心能存所有资源
    }
    if (dropSN < 0) {
        return false;    //连市镇中心都没有,那就没法算了
    }
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].SN == dropSN) {
            bx = info.buildings[i].BlockDR + 1;    //3x3建筑取中心格,距离更准
            by = info.buildings[i].BlockUR + 1;
            return true;
        }
    }
    return false;
}

/* =====================================================================
 *  找距离存放点最近的某类资源(采集效率优化)
 *  说明:以存放点为基准遍历该类资源,选最近的一个。村民从该资源采完
 *      返回存放点交资源的路程最短,单个采集周期耗时最少。
 *      exclude:本帧已被分配给其他村民的资源SN,避免多人抢同一块。
 * ===================================================================== */
int UsrAI::findResourceNearDropoff(const tagInfo& info, int resType, const std::set<int>& exclude)
{
    //先找存放点(浆果→谷仓,其他→仓库,都没有→市镇中心)
    int dropX = 0, dropY = 0;
    if (!findDropoff(info, resType, dropX, dropY)) {
        return -1;
    }
    //遍历该类资源,选距离存放点最近且未被本帧占用的
    int bestSN = -1;
    double bestDistance = 99999999.0;
    for (unsigned int i = 0; i < info.resources.size(); i++) {
        if (info.resources[i].Type != resType) {
            continue;    //不是这种资源,跳过
        }
        if (info.resources[i].Cnt <= 0) {
            continue;    //资源采完了,跳过
        }
        if (exclude.count(info.resources[i].SN) > 0) {
            continue;    //本帧已被别的村民选走,跳过
        }
        double d = distanceBlock(dropX, dropY, info.resources[i].BlockDR, info.resources[i].BlockUR);
        if (d < bestDistance) {
            bestDistance = d;
            bestSN = info.resources[i].SN;
        }
    }
    return bestSN;
}

//判断(x,y)这个位置能不能放下size*size的建筑
bool UsrAI::canBuildHere(const tagInfo& info, int x, int y, int size)
{
    //先检查有没有超出地图
    if (x < 0 || y < 0 || x + size > 100 || y + size > 100) {
        return false;
    }
    //检查里面的格子是不是都是草地,高度是不是一样
    int firstHeight = -999;
    for (int i = x; i < x + size; i++) {
        for (int j = y; j < y + size; j++) {
            if (info.theMap == NULL) {
                return false;
            }
            const tagTerrain& t = (*info.theMap)[i][j];
            if (t.type == MAPPATTERN_UNKNOWN) {
                return false;    //没探索过的地方不能建
            }
            if (t.type != MAPPATTERN_GRASS) {
                return false;    //不是草地不能建
            }
            if (firstHeight == -999) {
                firstHeight = t.height;
            } else {
                if (t.height != firstHeight) {
                    return false;    //高度不一样不能建
                }
            }
        }
    }
    //检查这个位置有没有被别的建筑占着,同时要求建筑之间至少留1格通道(不然单位会被卡住)
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        int bX = info.buildings[i].BlockDR;
        int bY = info.buildings[i].BlockUR;
        int bSize = 3;
        if (info.buildings[i].Type == BUILDING_HOME || info.buildings[i].Type == BUILDING_ARROWTOWER) {
            bSize = 2;    //房子和箭塔是2*2的
        }
        //把对方建筑的范围往外扩1格(周围留1格通道),再判断和目标建筑有没有重叠
        int bx1 = bX - 1;
        int by1 = bY - 1;
        int bx2 = bX + bSize + 1;
        int by2 = bY + bSize + 1;
        if (bx1 < x + size && bx2 > x && by1 < y + size && by2 > y) {
            return false;
        }
    }
    //检查有没有被敌人的建筑占着(同样留1格通道)
    for (unsigned int i = 0; i < info.enemy_buildings.size(); i++) {
        int bX = info.enemy_buildings[i].BlockDR;
        int bY = info.enemy_buildings[i].BlockUR;
        int bSize = 3;
        if (info.enemy_buildings[i].Type == BUILDING_HOME || info.enemy_buildings[i].Type == BUILDING_ARROWTOWER) {
            bSize = 2;
        }
        int bx1 = bX - 1;
        int by1 = bY - 1;
        int bx2 = bX + bSize + 1;
        int by2 = bY + bSize + 1;
        if (bx1 < x + size && bx2 > x && by1 < y + size && by2 > y) {
            return false;
        }
    }
    //检查有没有被资源(树、石头、金矿等)占着
    for (unsigned int i = 0; i < info.resources.size(); i++) {
        int rX = info.resources[i].BlockDR;
        int rY = info.resources[i].BlockUR;
        if (rX >= x && rX < x + size && rY >= y && rY < y + size) {
            return false;
        }
    }
    //检查有没有被村民占着
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        int fX = info.farmers[i].BlockDR;
        int fY = info.farmers[i].BlockUR;
        if (fX >= x && fX < x + size && fY >= y && fY < y + size) {
            return false;
        }
    }
    //检查有没有被自己的士兵占着
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        int aX = info.armies[i].BlockDR;
        int aY = info.armies[i].BlockUR;
        if (aX >= x && aX < x + size && aY >= y && aY < y + size) {
            return false;
        }
    }
    //检查有没有被敌人的士兵占着
    for (unsigned int i = 0; i < info.enemy_armies.size(); i++) {
        int aX = info.enemy_armies[i].BlockDR;
        int aY = info.enemy_armies[i].BlockUR;
        if (aX >= x && aX < x + size && aY >= y && aY < y + size) {
            return false;
        }
    }
    return true;    //都没有占着,可以建
}

//以(centerX,centerY)为中心,一圈一圈往外找能放下size*size建筑的空地
//找到就把坐标写到x和y里,返回true
bool UsrAI::findBuildPlace(const tagInfo& info, int& x, int& y, int size, int centerX, int centerY)
{
    //从半径4开始找,找到半径18,一圈一圈找(离市镇中心太近会被建筑围死)
    for (int r = 4; r <= 18; r++) {
        //先找上面那条边
        for (int dx = -r; dx <= r; dx++) {
            if (canBuildHere(info, centerX + dx, centerY - r, size)) {
                x = centerX + dx;
                y = centerY - r;
                return true;
            }
        }
        //再找下面那条边
        for (int dx = -r; dx <= r; dx++) {
            if (canBuildHere(info, centerX + dx, centerY + r, size)) {
                x = centerX + dx;
                y = centerY + r;
                return true;
            }
        }
        //再找左边那条边
        for (int dy = -r; dy <= r; dy++) {
            if (canBuildHere(info, centerX - r, centerY + dy, size)) {
                x = centerX - r;
                y = centerY + dy;
                return true;
            }
        }
        //再找右边那条边
        for (int dy = -r; dy <= r; dy++) {
            if (canBuildHere(info, centerX + r, centerY + dy, size)) {
                x = centerX + r;
                y = centerY + dy;
                return true;
            }
        }
    }
    return false;    //没找到能放的地方
}

/* =====================================================================
 *  找祭司的编号和位置
 *  说明:军队要守在他旁边(敌人的波次会专门来杀祭司),所以要经常找他
 * ===================================================================== */
bool UsrAI::findPriest(const tagInfo& info, int& sn, int& bx, int& by, double& dr, double& ur)
{
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        if (info.armies[i].Sort == AT_PRIEST) {
            sn = info.armies[i].SN;
            bx = info.armies[i].BlockDR;
            by = info.armies[i].BlockUR;
            dr = info.armies[i].DR;
            ur = info.armies[i].UR;
            return true;
        }
    }
    return false;
}

/* =====================================================================
 *  找祭司的安全点:有箭塔就去箭塔旁边,没有就去市镇中心旁边
 *  说明:祭司是胜利的关键,躲的地方要尽量有箭塔罩着
 * ===================================================================== */
bool UsrAI::findPriestSafeSpot(const tagInfo& info, int& x, int& y)
{
    //有箭塔就去箭塔旁边
    int towerSN = findBuilding(info, BUILDING_ARROWTOWER);
    if (towerSN >= 0) {
        for (unsigned int i = 0; i < info.buildings.size(); i++) {
            if (info.buildings[i].SN == towerSN) {
                x = info.buildings[i].BlockDR + 1;
                y = info.buildings[i].BlockUR + 1;
                return true;
            }
        }
    }
    //没有箭塔就去市镇中心旁边
    if (townX >= 0 && townY >= 0) {
        x = townX + 1;
        y = townY + 1;
        return true;
    }
    return false;
}

/* =====================================================================
 *  地图信息更新
 * ===================================================================== */
void UsrAI::updateMapInfo(const tagInfo& info)
{
    //把每个格子都先设成没探索过
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 100; j++) {
            gameMap[i][j] = -1;
        }
    }
    //根据theMap把地形填进去
    if (info.theMap == NULL) {
        return;
    }
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 100; j++) {
            const tagTerrain& t = (*info.theMap)[i][j];
            if (t.type == MAPPATTERN_GRASS) {
                gameMap[i][j] = 0;    //草地
            } else if (t.type == MAPPATTERN_OCEAN) {
                gameMap[i][j] = 1;    //海洋
            } else {
                gameMap[i][j] = -1;   //没探索过
            }
        }
    }
}

/* =====================================================================
 *  给空闲村民分配工作
 * ===================================================================== */
void UsrAI::assignWork(const tagInfo& info)
{
    //先把已经死掉的村民的记录删掉
    map<int,int>::iterator it = villagerWork.begin();
    while (it != villagerWork.end()) {
        int sn = it->first;
        bool alive = false;
        for (unsigned int i = 0; i < info.farmers.size(); i++) {
            if (info.farmers[i].SN == sn) {
                alive = true;
                break;
            }
        }
        if (alive) {
            it++;
        } else {
            it = villagerWork.erase(it);
        }
    }

    //统计一下现在每种工作各有多少人在干
    int numBerry = 0;   //采浆果的人数
    int numWood = 0;    //砍树的人数
    int numStone = 0;   //挖石头的人数
    int numHunt = 0;    //打猎的人数
    int numGold = 0;    //挖金矿的人数
    int numFarm = 0;    //种田的人数

    map<int,int>::iterator it2 = villagerWork.begin();
    while (it2 != villagerWork.end()) {
        int work = it2->second;
        if (work == WORK_BERRY) {
            numBerry++;
        } else if (work == WORK_WOOD) {
            numWood++;
        } else if (work == WORK_STONE) {
            numStone++;
        } else if (work == WORK_HUNT) {
            numHunt++;
        } else if (work == WORK_GOLD) {
            numGold++;
        } else if (work == WORK_FARM) {
            numFarm++;
        }
        it2++;
    }

    //数一数现在一共有多少个村民
    int villagerNum = 0;
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        if (info.farmers[i].FarmerSort == 0) {
            villagerNum++;
        }
    }

    //砍树的人要多留几个,盖房子、盖兵营、盖箭塔、盖农田都要木头
    int wantWood = 5;

    //收集已经被占用的农田(已在种田的农民的工作目标),让新分配的农民避开
    //(文档:一块农田只能一个村民采集,多人采不增加效率)
    set<int> occupiedFarm;
    for (unsigned int j = 0; j < info.farmers.size(); j++) {
        const tagFarmer& f = info.farmers[j];
        if (f.FarmerSort != 0) {
            continue;
        }
        map<int,int>::iterator wit = villagerWork.find(f.SN);
        if (wit != villagerWork.end() && wit->second == WORK_FARM && f.WorkObjectSN > 0) {
            occupiedFarm.insert(f.WorkObjectSN);
        }
    }
    //本帧内已被分配出去的农田(避免同帧多个空闲农民选同一块)
    set<int> farmChosenThisFrame;

    //本帧内已被分配出去的资源(避免同帧多个空闲农民抢同一棵树/浆果/石头/金矿)
    set<int> resourceChosenThisFrame;

    //挨个看每个村民,如果他是空闲的,就给他安排工作
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        const tagFarmer& farmer = info.farmers[i];
        //只看陆地村民,渔船和运输船不管
        if (farmer.FarmerSort != 0) {
            continue;
        }
        //如果他在忙,就不管他
        int state = farmer.NowState;
        bool isIdle = false;
        if (state == HUMAN_STATE_IDLE) {
            isIdle = true;
        } else if (state == HUMAN_STATE_WORKING) {
            //正在工作,但是工作对象不见了,也算空闲
            int workSN = farmer.WorkObjectSN;
            bool targetGone = true;
            for (unsigned int j = 0; j < info.resources.size(); j++) {
                if (info.resources[j].SN == workSN) {
                    targetGone = false;
                    break;
                }
            }
            if (targetGone) {
                for (unsigned int j = 0; j < info.buildings.size(); j++) {
                    if (info.buildings[j].SN == workSN) {
                        targetGone = false;
                        break;
                    }
                }
            }
            if (targetGone) {
                isIdle = true;
            }
        }
        if (!isIdle) {
            continue;
        }
        //盖房子的人由盖房子的函数管,这里不管
        map<int,int>::iterator workIt = villagerWork.find(farmer.SN);
        int nowWork = WORK_NONE;
        if (workIt != villagerWork.end()) {
            nowWork = workIt->second;
        }
        if (nowWork == WORK_BUILD) {
            continue;
        }
        //别每帧都给他下指令,隔几帧再下
        if (!canOrder(farmer.SN, 10)) {
            continue;
        }

        //根据现在的缺人情况,决定让他干什么活
        int chooseWork = WORK_WOOD;    //默认去砍树
        if (gameStage <= 2) {
            //阶段1+2(游戏开始+防御第一波):食物为主,金矿铜器后才需要
            //浆果4人(食物主力)、石头1人、打猎2人一组、农田2块、砍树上限5人
            if (numBerry < 4) {
                chooseWork = WORK_BERRY;
            } else if (numStone < 1) {
                chooseWork = WORK_STONE;
            } else if (numHunt < 2) {
                chooseWork = WORK_HUNT;
            } else if (numFarm < 2) {
                chooseWork = WORK_FARM;
            } else if (numWood < wantWood) {
                chooseWork = WORK_WOOD;
            } else {
                chooseWork = WORK_WOOD;    //保底砍树,不闲置
            }
        } else {
            //铜器时代:金矿3人、石头2人、农田6块(与buildFarm目标一致)、打猎2人、砍树补足
            if (numStone < 2) {
                chooseWork = WORK_STONE;
            } else if (numGold < 3) {
                chooseWork = WORK_GOLD;
            } else if (numHunt < 2) {
                chooseWork = WORK_HUNT;
            } else if (numFarm < 6) {
                chooseWork = WORK_FARM;
            } else if (numWood < wantWood) {
                chooseWork = WORK_WOOD;
            } else {
                chooseWork = WORK_WOOD;    //保底砍树,不闲置
            }
        }

        //看看这种资源还有没有,没有的话就去砍树
        int targetSN = -1;
        if (chooseWork == WORK_BERRY) {
            //采集效率:选离谷仓/市镇中心(存放点)最近的浆果,返程交浆果路程最短
            targetSN = findResourceNearDropoff(info, RESOURCE_BUSH, resourceChosenThisFrame);
        } else if (chooseWork == WORK_STONE) {
            //选离仓库/市镇中心最近的石头
            targetSN = findResourceNearDropoff(info, RESOURCE_STONE, resourceChosenThisFrame);
        } else if (chooseWork == WORK_HUNT) {
            //选离仓库/市镇中心最近的瞪羚
            targetSN = findResourceNearDropoff(info, RESOURCE_GAZELLE, resourceChosenThisFrame);
        } else if (chooseWork == WORK_GOLD) {
            //选离仓库/市镇中心最近的金矿
            targetSN = findResourceNearDropoff(info, RESOURCE_GOLD, resourceChosenThisFrame);
        } else if (chooseWork == WORK_FARM) {
            //种田:每人一块田,避开已经被占用的农田;选离谷仓/市镇中心最近的田,返程交粮路程最短
            int dropX = 0, dropY = 0;
            if (!findDropoff(info, RESOURCE_BUSH, dropX, dropY)) {
                dropX = farmer.BlockDR;    //谷仓和市镇中心都没有,退回按村民位置选
                dropY = farmer.BlockUR;
            }
            int bestFarmSN = -1;
            double bestFarmDist = 99999999.0;
            for (unsigned int j = 0; j < info.buildings.size(); j++) {
                const tagBuilding& b = info.buildings[j];
                if (b.Type != BUILDING_FARM || b.Percent < 100 || b.Cnt <= 0) {
                    continue;
                }
                if (occupiedFarm.count(b.SN) > 0 || farmChosenThisFrame.count(b.SN) > 0) {
                    continue;    //这块田已经有人种了,换一块
                }
                double d = distanceBlock(dropX, dropY, b.BlockDR, b.BlockUR);
                if (d < bestFarmDist) {
                    bestFarmDist = d;
                    bestFarmSN = b.SN;
                }
            }
            if (bestFarmSN >= 0) {
                targetSN = bestFarmSN;
                farmChosenThisFrame.insert(bestFarmSN);    //本帧内不再分给其他农民
            }
        } else {
            //选离仓库/市镇中心最近的树
            targetSN = findResourceNearDropoff(info, RESOURCE_TREE, resourceChosenThisFrame);
        }

        //没找到目标资源,就改成砍树
        if (targetSN < 0) {
            if (chooseWork != WORK_WOOD) {
                chooseWork = WORK_WOOD;
                targetSN = findResourceNearDropoff(info, RESOURCE_TREE, resourceChosenThisFrame);
            }
        }
        if (targetSN < 0) {
            continue;    //连树都没有,那就没办法了
        }

        //下指令让他去干活
        HumanAction(farmer.SN, targetSN);
        rememberOrder(farmer.SN);
        villagerWork[farmer.SN] = chooseWork;
        resourceChosenThisFrame.insert(targetSN);    //本帧不再分给其他村民
    }
}

/* =====================================================================
 *  派一个村民去盖房子
 * ===================================================================== */
void UsrAI::buildHouse(const tagInfo& info)
{
    //看看现在有几间房子,前期盖到6间,铜器后盖到12间(每间4人口,房子少了人口上限不够)
    int houseNum = countBuilding(info, BUILDING_HOME);
    int wantHouse = 6;
    if (gameStage >= 3) {
        wantHouse = 12;    //铜器后(阶段2+)12间房子
    }
    if (houseNum >= wantHouse) {
        return;    //房子够了
    }
    //木材不够就先不盖
    if (info.Wood < BUILD_HOUSE_WOOD) {
        return;
    }
    //找一个空闲的村民来盖
    int workerSN = -1;
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        const tagFarmer& farmer = info.farmers[i];
        if (farmer.FarmerSort != 0) {
            continue;
        }
        map<int,int>::iterator it = villagerWork.find(farmer.SN);
        int nowWork = WORK_NONE;
        if (it != villagerWork.end()) {
            nowWork = it->second;
        }
        //优先找之前就在盖房子的人,找不到就找空闲的
        if (nowWork == WORK_BUILD && farmer.NowState == HUMAN_STATE_IDLE) {
            workerSN = farmer.SN;
            break;
        }
    }
    if (workerSN < 0) {
        for (unsigned int i = 0; i < info.farmers.size(); i++) {
            const tagFarmer& farmer = info.farmers[i];
            if (farmer.FarmerSort != 0) {
                continue;
            }
            if (farmer.NowState == HUMAN_STATE_IDLE) {
                workerSN = farmer.SN;
                break;
            }
        }
    }
    if (workerSN < 0) {
        return;    //没有空闲的村民
    }
    if (!canOrder(workerSN, 15)) {
        return;
    }
    //找一块能放2*2房子的空地
    int bx = 0;
    int by = 0;
    if (!findBuildPlace(info, bx, by, 2, townX, townY)) {
        return;
    }
    //下指令盖房子
    HumanBuild(workerSN, BUILDING_HOME, bx, by);
    rememberOrder(workerSN);
    villagerWork[workerSN] = WORK_BUILD;
}

/* =====================================================================
 *  派一个村民去盖其他建筑
 * ===================================================================== */
void UsrAI::buildSomeBuilding(const tagInfo& info, int buildingType)
{
    //这个建筑已经建好了就不用再建了
    int builtNum = 0;
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].Type == buildingType && info.buildings[i].Percent >= 100) {
            builtNum++;
        }
    }
    if (builtNum >= 1) {
        return;
    }
    //检查木材够不够
    if (buildingType == BUILDING_MARKET && info.Wood < BUILD_MARKET_WOOD) {
        return;
    }
    if (buildingType == BUILDING_ARMYCAMP && info.Wood < BUILD_ARMYCAMP_WOOD) {
        return;
    }
    if (buildingType == BUILDING_RANGE && info.Wood < BUILD_RANGE_WOOD) {
        return;
    }
    if (buildingType == BUILDING_STABLE && info.Wood < BUILD_STABLE_WOOD) {
        return;
    }
    if (buildingType == BUILDING_COLLAGE && info.Wood < BUILD_COLLAGE_WOOD) {
        return;
    }
    if (buildingType == BUILDING_ARROWTOWER && info.Stone < BUILD_ARROWTOWER_STONE) {
        return;
    }
    //看看有没有正在盖的,有的话就等它盖完
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].Type == buildingType && info.buildings[i].Percent < 100) {
            return;
        }
    }
    //找一个空闲村民
    int workerSN = -1;
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        const tagFarmer& farmer = info.farmers[i];
        if (farmer.FarmerSort != 0) {
            continue;
        }
        map<int,int>::iterator it = villagerWork.find(farmer.SN);
        int nowWork = WORK_NONE;
        if (it != villagerWork.end()) {
            nowWork = it->second;
        }
        if (nowWork == WORK_BUILD && farmer.NowState == HUMAN_STATE_IDLE) {
            workerSN = farmer.SN;
            break;
        }
    }
    if (workerSN < 0) {
        for (unsigned int i = 0; i < info.farmers.size(); i++) {
            const tagFarmer& farmer = info.farmers[i];
            if (farmer.FarmerSort != 0) {
                continue;
            }
            if (farmer.NowState == HUMAN_STATE_IDLE) {
                workerSN = farmer.SN;
                break;
            }
        }
    }
    if (workerSN < 0) {
        return;
    }
    if (!canOrder(workerSN, 15)) {
        return;
    }
    //判断建筑是多大(房子和箭塔2*2,其他3*3)
    int size = 3;
    if (buildingType == BUILDING_ARROWTOWER) {
        size = 2;
    }
    //找空地
    int bx = 0;
    int by = 0;
    if (!findBuildPlace(info, bx, by, size, townX, townY)) {
        return;
    }
    //下指令
    HumanBuild(workerSN, buildingType, bx, by);
    rememberOrder(workerSN);
    villagerWork[workerSN] = WORK_BUILD;
}

/* =====================================================================
 *  派一个村民去盖农田
 *  说明:浆果和猎物会采完,不种田的话后期没食物,村民和士兵都造不出来。
 *      农田需要先建好市场才能盖。
 * ===================================================================== */
void UsrAI::buildFarm(const tagInfo& info)
{
    //农田的前置是市场,没有市场就不盖
    if (findBuilding(info, BUILDING_MARKET) < 0) {
        return;
    }
    //数一数现在还有食物的农田,前期留2块,铜器后留6块
    int liveFarm = 0;
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].Type == BUILDING_FARM && info.buildings[i].Percent >= 100
            && info.buildings[i].Cnt > 0) {
            liveFarm++;
        }
    }
    int wantFarm = 2;
    if (gameStage >= 3) {
        wantFarm = 6;    //铜器后(阶段2+)6块农田
    }
    if (liveFarm >= wantFarm) {
        return;
    }
    //木材不够就先不盖
    if (info.Wood < BUILD_FARM_WOOD) {
        return;
    }
    //有正在盖的农田就先等它盖完,别一次派好几个村民去盖
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].Type == BUILDING_FARM && info.buildings[i].Percent < 100) {
            return;
        }
    }
    //找一个空闲的村民来盖(优先之前就在盖建筑的人)
    int workerSN = -1;
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        const tagFarmer& farmer = info.farmers[i];
        if (farmer.FarmerSort != 0) {
            continue;
        }
        map<int,int>::iterator it = villagerWork.find(farmer.SN);
        int nowWork = WORK_NONE;
        if (it != villagerWork.end()) {
            nowWork = it->second;
        }
        if (nowWork == WORK_BUILD && farmer.NowState == HUMAN_STATE_IDLE) {
            workerSN = farmer.SN;
            break;
        }
    }
    if (workerSN < 0) {
        for (unsigned int i = 0; i < info.farmers.size(); i++) {
            const tagFarmer& farmer = info.farmers[i];
            if (farmer.FarmerSort != 0) {
                continue;
            }
            if (farmer.NowState == HUMAN_STATE_IDLE) {
                workerSN = farmer.SN;
                break;
            }
        }
    }
    if (workerSN < 0) {
        return;    //没有空闲的村民
    }
    if (!canOrder(workerSN, 15)) {
        return;
    }
    //找一块3*3的空地,优先贴着市场盖(离得近好采集),不行就贴着市镇中心
    int centerX = townX;
    int centerY = townY;
    int marketSN = findBuilding(info, BUILDING_MARKET);
    if (marketSN >= 0) {
        for (unsigned int i = 0; i < info.buildings.size(); i++) {
            if (info.buildings[i].SN == marketSN) {
                centerX = info.buildings[i].BlockDR;
                centerY = info.buildings[i].BlockUR;
                break;
            }
        }
    }
    int bx = 0;
    int by = 0;
    if (!findBuildPlace(info, bx, by, 3, centerX, centerY)) {
        if (!findBuildPlace(info, bx, by, 3, townX, townY)) {
            return;    //实在找不到空地
        }
    }
    //下指令盖农田
    HumanBuild(workerSN, BUILDING_FARM, bx, by);
    rememberOrder(workerSN);
    villagerWork[workerSN] = WORK_BUILD;
}

/* =====================================================================
 *  在已有箭塔周围额外建造箭塔（增强防守），形成三角形布局
 * ===================================================================== */
void UsrAI::buildExtraTowers(const tagInfo& info)
{
    // 必须已解锁箭塔科技
    if (!hasArrowTech) {
        return;
    }

    // 统计当前已建成的箭塔数量，并记录第一个箭塔坐标作为参考
    int towerCount = 0;
    int refX = -1, refY = -1;
    for (const auto& b : info.buildings) {
        if (b.Type == BUILDING_ARROWTOWER && b.Percent >= 100) {
            towerCount++;
            if (refX == -1) {
                refX = b.BlockDR;
                refY = b.BlockUR;
            }
        }
    }

    // 目标数量：3个（初始1个 + 额外2个），太多会挤在一起把单位卡住
    const int TARGET_TOWERS = 3;
    if (towerCount >= TARGET_TOWERS) {
        return;
    }

    // 检查石头是否足够
    if (info.Stone < 150) {
        return;
    }

    // 找一个空闲的陆地村民
    int workerSN = -1;
    for (const auto& farmer : info.farmers) {
        if (farmer.FarmerSort != FARMERTYPE_FARMER) continue;
        if (farmer.NowState == HUMAN_STATE_IDLE) {
            workerSN = farmer.SN;
            break;
        }
    }
    if (workerSN < 0) {
        return;
    }

    // 确定参考中心：用第一个箭塔坐标，若无则用市镇中心
    int centerX = (refX >= 0) ? refX : townX;
    int centerY = (refY >= 0) ? refY : townY;
    if (centerX < 0 || centerY < 0) {
        return;
    }

    // 三个不同的偏移方向（相对于参考中心），确保三点不共线
    const int OFFSET_COUNT = 3;
    const int offX[OFFSET_COUNT] = { 3,  3, -3 };
    const int offY[OFFSET_COUNT] = { 3, -3,  3 };

    // 根据当前已有箭塔数量选择主偏移索引
    // 若 towerCount == 1（建第二个塔）→ 索引0；若 towerCount == 2（建第三个塔）→ 索引1
    int mainIdx = (towerCount == 1) ? 0 : 1;
    if (mainIdx < 0) mainIdx = 0;
    if (mainIdx >= OFFSET_COUNT) mainIdx = 0;

    int bx = 0, by = 0;
    bool placed = false;

    // 优先尝试主偏移
    if (findBuildPlace(info, bx, by, 2, centerX + offX[mainIdx], centerY + offY[mainIdx])) {
        placed = true;
    }

    // 若主偏移失败，尝试其他两个偏移（按顺序）
    if (!placed) {
        for (int i = 1; i < OFFSET_COUNT; ++i) {
            int idx = (mainIdx + i) % OFFSET_COUNT;
            if (findBuildPlace(info, bx, by, 2, centerX + offX[idx], centerY + offY[idx])) {
                placed = true;
                break;
            }
        }
    }

    // 若所有偏移都失败，最后尝试以参考中心本身为搜索中心（通常不会用到）
    if (!placed) {
        if (findBuildPlace(info, bx, by, 2, centerX, centerY)) {
            placed = true;
        }
    }

    if (!placed) {
        return;
    }

    // 控制指令频率并建造
    if (!canOrder(workerSN, 15)) {
        return;
    }
    HumanBuild(workerSN, BUILDING_ARROWTOWER, bx, by);
    rememberOrder(workerSN);
}

/* =====================================================================
 *  研究科技
 *  思路:造兵永远比研究科技优先。第二波之前只研究箭塔科技、
 *      箭塔升级和工具使用,其他科技全部推后,把食物和
 *      建筑时间省下来造兵,不然波次来了兵不够。
 * ===================================================================== */
void UsrAI::researchTech(const tagInfo& info)
{
    //谷仓研究箭塔科技(这样村民才能盖箭塔,越早越好)
    if (!hasArrowTech && info.Meat >= BUILDING_GRANARY_ARROWTOWER_FOOD) {
        int granarySN = findBuilding(info, BUILDING_GRANARY);
        if (granarySN >= 0) {
            //看看谷仓有没有在忙
            bool busy = false;
            for (unsigned int i = 0; i < info.buildings.size(); i++) {
                if (info.buildings[i].SN == granarySN && info.buildings[i].Project != 0) {
                    busy = true;
                    break;
                }
            }
            if (!busy) {
                BuildingAction(granarySN, BUILDING_GRANARY_ARROWTOWER);
                hasArrowTech = true;
            }
        }
    }

    //谷仓研究箭塔升级(铜器时代,箭塔加1攻击加1射程,第二波前研究完)
    if (gameStage >= 3 && !hasTowerUpTech
        && info.Meat >= BUILDING_GRANARY_UPGRADE_ARROWTOWER_FOOD
        && info.Stone >= BUILDING_GRANARY_UPGRADE_ARROWTOWER_STONE) {
        int granarySN = findBuilding(info, BUILDING_GRANARY);
        if (granarySN >= 0) {
            bool busy = false;
            for (unsigned int i = 0; i < info.buildings.size(); i++) {
                if (info.buildings[i].SN == granarySN && info.buildings[i].Project != 0) {
                    busy = true;
                    break;
                }
            }
            if (!busy) {
                BuildingAction(granarySN, BUILDING_GRANARY_ARROWTOWE_UPGRADE);
                hasTowerUpTech = true;
            }
        }
    }

    //仓库研究工具使用(加攻击,便宜,前期就研究)
    if (!hasToolTech && info.Meat >= BUILDING_STOCK_UPGRADE_CLOSER_ATTACK_FOOD) {
        int stockSN = findBuilding(info, BUILDING_STOCK);
        if (stockSN >= 0) {
            bool busy = false;
            for (unsigned int i = 0; i < info.buildings.size(); i++) {
                if (info.buildings[i].SN == stockSN && info.buildings[i].Project != 0) {
                    busy = true;
                    break;
                }
            }
            if (!busy) {
                BuildingAction(stockSN, BUILDING_STOCK_UPGRADE_USETOOL);
                hasToolTech = true;
            }
        }
    }

    //========== 第一波(6000帧)打完后:兵营战斧升级 ==========
    if (info.GameFrame >= 6000 && !hasAxeTech && info.Meat >= BUILDING_ARMYCAMP_UPGRADE_CLUBMAN_FOOD) {
        int campSN = findBuilding(info, BUILDING_ARMYCAMP);
        if (campSN >= 0) {
            bool busy = false;
            for (unsigned int i = 0; i < info.buildings.size(); i++) {
                if (info.buildings[i].SN == campSN && info.buildings[i].Project != 0) {
                    busy = true;
                    break;
                }
            }
            if (!busy) {
                BuildingAction(campSN, BUILDING_ARMYCAMP_UPGRADE_CLUBMAN);
                hasAxeTech = true;
            }
        }
    }

    //========== 第二波(13500帧)打完后:阔剑、复合弓、步兵护甲 ==========
    if (info.GameFrame >= 13500) {
        if (!hasBroadTech && info.Meat >= BUILDING_ARMYCAMP_UPGRADE_BROADSWORD_FOOD
            && info.Gold >= BUILDING_ARMYCAMP_UPGRADE_BROADSWORD_GOLD) {
            int campSN = findBuilding(info, BUILDING_ARMYCAMP);
            if (campSN >= 0) {
                bool busy = false;
                for (unsigned int i = 0; i < info.buildings.size(); i++) {
                    if (info.buildings[i].SN == campSN && info.buildings[i].Project != 0) {
                        busy = true;
                        break;
                    }
                }
                if (!busy) {
                    BuildingAction(campSN, BUILDING_ARMYCAMP_UPGRADE_BROADSWORD);
                    hasBroadTech = true;
                }
            }
        }
        if (!hasCompositeTech && info.Meat >= BUILDING_RANGE_UPGRADE_COMPOSITE_BOW_FOOD
            && info.Wood >= BUILDING_RANGE_UPGRADE_COMPOSITE_BOW_WOOD) {
            int rangeSN = findBuilding(info, BUILDING_RANGE);
            if (rangeSN >= 0) {
                bool busy = false;
                for (unsigned int i = 0; i < info.buildings.size(); i++) {
                    if (info.buildings[i].SN == rangeSN && info.buildings[i].Project != 0) {
                        busy = true;
                        break;
                    }
                }
                if (!busy) {
                    BuildingAction(rangeSN, BUILDING_RANGE_UPGRADE_COMPOSITE_BOW);
                    hasCompositeTech = true;
                }
            }
        }
        if (!hasInfTech && info.Meat >= BUILDING_STOCK_UPGRADE_DEFENSE_INFANTRY_FOOD) {
            int stockSN = findBuilding(info, BUILDING_STOCK);
            if (stockSN >= 0) {
                bool busy = false;
                for (unsigned int i = 0; i < info.buildings.size(); i++) {
                    if (info.buildings[i].SN == stockSN && info.buildings[i].Project != 0) {
                        busy = true;
                        break;
                    }
                }
                if (!busy) {
                    BuildingAction(stockSN, BUILDING_STOCK_UPGRADE_DEFENSE_INFANTRY);
                    hasInfTech = true;
                }
            }
        }
    }

    //========== 第三波(21000帧)打完后:市场科技(伐木、车轮、金矿、驯养动物) ==========
    if (info.GameFrame >= 21000) {
        if (!hasWoodTech && info.Meat >= BUILDING_MARKET_WOOD_UPGRADE_FOOD
            && info.Wood >= BUILDING_MARKET_WOOD_UPGRADE_WOOD) {
            int marketSN = findBuilding(info, BUILDING_MARKET);
            if (marketSN >= 0) {
                bool busy = false;
                for (unsigned int i = 0; i < info.buildings.size(); i++) {
                    if (info.buildings[i].SN == marketSN && info.buildings[i].Project != 0) {
                        busy = true;
                        break;
                    }
                }
                if (!busy) {
                    BuildingAction(marketSN, BUILDING_MARKET_WOOD_UPGRADE);
                    hasWoodTech = true;
                }
            }
        }
        if (!hasWheelTech && info.Meat >= BUILDING_MARKET_WHEEL_UPGRADE_FOOD
            && info.Wood >= BUILDING_MARKET_WHEEL_UPGRADE_WOOD) {
            int marketSN = findBuilding(info, BUILDING_MARKET);
            if (marketSN >= 0) {
                bool busy = false;
                for (unsigned int i = 0; i < info.buildings.size(); i++) {
                    if (info.buildings[i].SN == marketSN && info.buildings[i].Project != 0) {
                        busy = true;
                        break;
                    }
                }
                if (!busy) {
                    BuildingAction(marketSN, BUILDING_MARKET_WHEEL_UPGRADE);
                    hasWheelTech = true;
                }
            }
        }
        if (!hasGoldTech && info.Meat >= BUILDING_MARKET_GOLD_UPGRADE_FOOD
            && info.Wood >= BUILDING_MARKET_GOLD_UPGRADE_WOOD) {
            int marketSN = findBuilding(info, BUILDING_MARKET);
            if (marketSN >= 0) {
                bool busy = false;
                for (unsigned int i = 0; i < info.buildings.size(); i++) {
                    if (info.buildings[i].SN == marketSN && info.buildings[i].Project != 0) {
                        busy = true;
                        break;
                    }
                }
                if (!busy) {
                    BuildingAction(marketSN, BUILDING_MARKET_GOLD_UPGRADE);
                    hasGoldTech = true;
                }
            }
        }
        if (!hasFarmTech && info.Meat >= BUILDING_MARKET_FARM_UPGRADE_FOOD
            && info.Wood >= BUILDING_MARKET_FARM_UPGRADE_WOOD) {
            int marketSN = findBuilding(info, BUILDING_MARKET);
            if (marketSN >= 0) {
                bool busy = false;
                for (unsigned int i = 0; i < info.buildings.size(); i++) {
                    if (info.buildings[i].SN == marketSN && info.buildings[i].Project != 0) {
                        busy = true;
                        break;
                    }
                }
                if (!busy) {
                    BuildingAction(marketSN, BUILDING_MARKET_FARM_UPGRADE);
                    hasFarmTech = true;
                }
            }
        }
    }
}

/* =====================================================================
 *  让市镇中心生产村民
 * ===================================================================== */
void UsrAI::makeVillager(const tagInfo& info)
{
    int centerSN = findBuilding(info, BUILDING_CENTER);
    if (centerSN < 0) {
        return;
    }
    //看看市镇中心现在有没有在忙
    bool busy = false;
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].SN == centerSN && info.buildings[i].Project != 0) {
            busy = true;
            break;
        }
    }
    if (busy) {
        return;
    }
    //数一数现在有多少村民
    int villagerNum = 0;
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        if (info.farmers[i].FarmerSort == 0) {
            villagerNum++;
        }
    }
    //村民养到14个就够了,铜器后16个,剩下的人口全部用来造兵
    //(兵少了第二波都顶不住,经济够用就行)
    int wantVillager = 14;
    if (gameStage >= 3) {
        wantVillager = 16;    //铜器后(阶段2+)16个村民
    }
    //食物够,村民不够,人口没满,就可以生产
    if (villagerNum < wantVillager && info.Meat >= BUILDING_CENTER_CREATEFARMER_FOOD) {
        if (info.Human_Num + 1 <= info.Human_MaxNum) {
            BuildingAction(centerSN, BUILDING_CENTER_CREATEFARMER);
        }
    }
}

/* =====================================================================
 *  让市镇中心升级时代(工具时代 -> 铜器时代)
 * ===================================================================== */
void UsrAI::upgradeAge(const tagInfo& info)
{
    //已经下过升级指令了,或者已经不是工具时代了,就不管了
    if (hasAgeUp) {
        return;
    }
    if (info.civilizationStage != CIVILIZATION_TOOLAGE) {
        return;
    }
    //升级要800食物
    if (info.Meat < 800) {
        return;
    }
    //还要先建好市场、马厩、靶场中的两个(游戏规则要求的)
    int toolNum = 0;
    if (findBuilding(info, BUILDING_MARKET) >= 0) {
        toolNum++;
    }
    if (findBuilding(info, BUILDING_STABLE) >= 0) {
        toolNum++;
    }
    if (findBuilding(info, BUILDING_RANGE) >= 0) {
        toolNum++;
    }
    if (toolNum < 2) {
        return;    //工具时代的建筑还没建够
    }
    int centerSN = findBuilding(info, BUILDING_CENTER);
    if (centerSN < 0) {
        return;
    }
    bool busy = false;
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].SN == centerSN && info.buildings[i].Project != 0) {
            busy = true;
            break;
        }
    }
    if (busy) {
        return;
    }
    //下指令升级
    BuildingAction(centerSN, BUILDING_CENTER_UPGRADE);
    hasAgeUp = true;
}

/* =====================================================================
 *  让兵营、靶场、马厩、学院生产士兵
 *  思路:造兵永远比研究科技优先,兵少什么都守不住。
 *       - 工具时代就造棍棒兵+弓箭手保底(第一波4分钟就来)
 *       - 铜器时代主力是方阵兵(血厚攻高)和弓箭手,棍棒兵当炮灰
 *       - 目标跟着波次走:第一波前6个,第二波前18个,第三波前22个
 * ===================================================================== */
void UsrAI::makeArmy(const tagInfo& info)
{
    //人口满了就不能造了
    if (info.Human_Num + 1 > info.Human_MaxNum) {
        return;
    }
    //想养多少兵(总数包括祭司和侦察兵),跟着波次走
    int wantArmy = 6;                 //第一波前:6个
    if (info.GameFrame >= 13500) {
        wantArmy = 18;                //第二波前:18个
    }
    if (info.GameFrame >= 21000) {
        wantArmy = 22;                //第三波前:22个
    }
    if (gameStage == 4) {
        wantArmy = 26;                //反攻(阶段4):26个
    }
    if ((int)info.armies.size() >= wantArmy) {
        return;
    }

    //========== 学院:方阵兵(铜器主力,血厚攻高,顶在第一排) ==========
    if (gameStage >= 3) {
        int collageSN = findBuilding(info, BUILDING_COLLAGE);
        if (collageSN >= 0) {
            bool collageBusy = false;
            for (unsigned int i = 0; i < info.buildings.size(); i++) {
                if (info.buildings[i].SN == collageSN && info.buildings[i].Project != 0) {
                    collageBusy = true;
                    break;
                }
            }
            if (!collageBusy) {
                if (countArmy(info, AT_HOPLITE) < 8) {
                    if (info.Meat >= BUILDING_COLLAGE_CREATE_HOPLITE_FOOD
                        && info.Gold >= BUILDING_COLLAGE_CREATE_HOPLITE_GOLD) {
                        BuildingAction(collageSN, BUILDING_COLLAGE_CREATE_HOPLITE);
                        return;
                    }
                }
            }
        }
    }

    //========== 靶场:弓箭手(远程输出),有复合弓科技就造复合弓兵 ==========
    int rangeSN = findBuilding(info, BUILDING_RANGE);
    if (rangeSN >= 0) {
        bool rangeBusy = false;
        for (unsigned int i = 0; i < info.buildings.size(); i++) {
            if (info.buildings[i].SN == rangeSN && info.buildings[i].Project != 0) {
                rangeBusy = true;
                break;
            }
        }
        if (!rangeBusy) {
            int bowmanNum = countArmy(info, AT_COMPOSITE_BOWMAN) + countArmy(info, AT_BOWMAN);
            if (hasCompositeTech && countArmy(info, AT_COMPOSITE_BOWMAN) < 6) {
                if (info.Meat >= BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN_FOOD
                    && info.Gold >= BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN_GOLD) {
                    BuildingAction(rangeSN, BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN);
                    return;
                }
            }
            if (countArmy(info, AT_BOWMAN) < 6 && bowmanNum < 10) {
                if (info.Meat >= BUILDING_RANGE_CREATE_BOWMAN_FOOD
                    && info.Wood >= BUILDING_RANGE_CREATE_BOWMAN_WOOD) {
                    BuildingAction(rangeSN, BUILDING_RANGE_CREATE_BOWMAN);
                    return;
                }
            }
        }
    }

    //========== 兵营:棍棒兵(便宜的炮灰),有阔剑科技就造阔剑兵 ==========
    int campSN = findBuilding(info, BUILDING_ARMYCAMP);
    if (campSN >= 0) {
        bool campBusy = false;
        for (unsigned int i = 0; i < info.buildings.size(); i++) {
            if (info.buildings[i].SN == campSN && info.buildings[i].Project != 0) {
                campBusy = true;
                break;
            }
        }
        if (!campBusy) {
            if (hasBroadTech && countArmy(info, AT_BROADSWORDSMAN) < 6) {
                if (info.Meat >= BUILDING_ARMYCAMP_CREATE_BROADSWORD_FOOD
                    && info.Gold >= BUILDING_ARMYCAMP_CREATE_BROADSWORD_GOLD) {
                    BuildingAction(campSN, BUILDING_ARMYCAMP_CREATE_BROADSWORD);
                    return;
                }
            }
            if (countArmy(info, AT_CLUBMAN) < 8) {
                if (info.Meat >= BUILDING_ARMYCAMP_CREATE_CLUBMAN_FOOD) {
                    BuildingAction(campSN, BUILDING_ARMYCAMP_CREATE_CLUBMAN);
                    return;
                }
            }
        }
    }

    //========== 马厩:侦察骑兵(探路用,2个就够) ==========
    if (gameStage >= 3) {
        int stableSN = findBuilding(info, BUILDING_STABLE);
        if (stableSN >= 0) {
            bool stableBusy = false;
            for (unsigned int i = 0; i < info.buildings.size(); i++) {
                if (info.buildings[i].SN == stableSN && info.buildings[i].Project != 0) {
                    stableBusy = true;
                    break;
                }
            }
            if (!stableBusy) {
                if (countArmy(info, AT_SCOUT) < 2) {
                    if (info.Meat >= BUILDING_STABLE_CREATE_SCOUT_FOOD) {
                        BuildingAction(stableSN, BUILDING_STABLE_CREATE_SCOUT);
                        return;
                    }
                }
            }
        }
    }
}

/* =====================================================================
 *  防守:军队围着祭司布防(祭司堡垒)
 *  说明:敌方波次会专门来杀祭司,所以军队就守在祭司旁边——
 *      敌人想杀祭司就必须先打穿我们的军队。没有敌人时全员在
 *      "祭司与敌人之间"的集结点待命,有敌人时全军集火最近的敌人。
 *      反攻阶段(阶段4)暂未实现,后续再补。
 * ===================================================================== */
void UsrAI::armyFight(const tagInfo& info)
{
    //反攻阶段(阶段4)暂未实现,当前只负责防守布防
    if (gameStage == 4) {
        return;
    }

    //找祭司(军队要守在他旁边)
    int priestSN = -1;
    int priestX = 0;
    int priestY = 0;
    double priestDR = 0.0;
    double priestUR = 0.0;
    if (!findPriest(info, priestSN, priestX, priestY, priestDR, priestUR)) {
        return;    //没有祭司(祭司死了游戏就输了)
    }

    //找祭司的安全点,集结点放在安全点朝敌人方向偏2格,让军队挡在中间
    int safeX = 0;
    int safeY = 0;
    if (!findPriestSafeSpot(info, safeX, safeY)) {
        return;
    }
    int rallyX = safeX + 2;
    int rallyY = safeY + 2;
    if (rallyX < 1) rallyX = 1;
    if (rallyY < 1) rallyY = 1;
    if (rallyX > 98) rallyX = 98;
    if (rallyY > 98) rallyY = 98;

    //在祭司周围20格内找最近的敌人(先军队,再农民,最后建筑)
    int enemySN = -1;
    int enemyX = -1;
    int enemyY = -1;
    double bestDistance = 99999999.0;
    for (unsigned int i = 0; i < info.enemy_armies.size(); i++) {
        double d = distanceBlock(info.enemy_armies[i].BlockDR, info.enemy_armies[i].BlockUR, priestX, priestY);
        if (d <= 20.0 && d < bestDistance) {
            bestDistance = d;
            enemySN = info.enemy_armies[i].SN;
            enemyX = info.enemy_armies[i].BlockDR;
            enemyY = info.enemy_armies[i].BlockUR;
        }
    }
    for (unsigned int i = 0; i < info.enemy_farmers.size(); i++) {
        double d = distanceBlock(info.enemy_farmers[i].BlockDR, info.enemy_farmers[i].BlockUR, priestX, priestY);
        if (d <= 20.0 && d < bestDistance) {
            bestDistance = d;
            enemySN = info.enemy_farmers[i].SN;
            enemyX = info.enemy_farmers[i].BlockDR;
            enemyY = info.enemy_farmers[i].BlockUR;
        }
    }
    if (enemySN < 0) {
        for (unsigned int i = 0; i < info.enemy_buildings.size(); i++) {
            double d = distanceBlock(info.enemy_buildings[i].BlockDR, info.enemy_buildings[i].BlockUR, priestX, priestY);
            if (d <= 20.0 && d < bestDistance) {
                bestDistance = d;
                enemySN = info.enemy_buildings[i].SN;
                enemyX = info.enemy_buildings[i].BlockDR;
                enemyY = info.enemy_buildings[i].BlockUR;
            }
        }
    }

    if (enemySN < 0) {
        //祭司附近没有敌人:所有空闲士兵去集结点集合,别散在外面
        for (unsigned int i = 0; i < info.armies.size(); i++) {
            const tagArmy& army = info.armies[i];
            if (army.Sort == AT_PRIEST || army.Sort == AT_SCOUT) {
                continue;    //祭司和侦察兵不管
            }
            if (army.NowState != HUMAN_STATE_IDLE) {
                continue;
            }
            if (distanceBlock(army.BlockDR, army.BlockUR, rallyX, rallyY) > 4.0) {
                if (canOrder(army.SN, 60)) {
                    HumanMove(army.SN, detailOf(rallyX), detailOf(rallyY));
                    rememberOrder(army.SN);
                }
            }
        }
        return;
    }

    //有敌人:全军集火最近的敌人(大家本来就在祭司附近,不会跑散)
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        const tagArmy& army = info.armies[i];
        if (army.Sort == AT_PRIEST) {
            continue;    //祭司不打架,他是用来加血和转化的
        }
        if (army.NowState != HUMAN_STATE_IDLE && army.NowState != HUMAN_STATE_WALKING) {
            continue;    //已经在打了就不重复下命令
        }
        //离战场太远的兵不过来(比如刚造出来的),免得来回跑
        if (distanceBlock(army.BlockDR, army.BlockUR, enemyX, enemyY) > 25.0) {
            continue;
        }
        if (!canOrder(army.SN, 8)) {
            continue;
        }
        HumanAction(army.SN, enemySN);
        rememberOrder(army.SN);
    }
}

/* =====================================================================
 *  箭塔自动攻击射程内的敌人
 *  说明:箭塔不会自己打人,要AI用HumanAction给它下攻击指令。
 *      每次只看射程内(DIS_ARROWTOWER格)的敌人,避免箭塔干等。
 * ===================================================================== */
void UsrAI::towerFight(const tagInfo& info)
{
    //箭塔的攻击距离(单位:格,和游戏里箭塔的射程一样)
    const double towerRange = double(DIS_ARROWTOWER);

    //挨个看每座箭塔
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        const tagBuilding& building = info.buildings[i];
        //只看已经建好的箭塔
        if (building.Type != BUILDING_ARROWTOWER) {
            continue;
        }
        if (building.Percent < 100) {
            continue;
        }
        //Project是箭塔当前攻击目标的编号,不是-1就说明它已经在打谁了
        if (building.Project >= 0) {
            continue;
        }
        //别每帧都下指令
        if (!canOrder(building.SN, 10)) {
            continue;
        }

        //箭塔中心的位置(箭塔是2*2的,中心在(BlockDR+1,BlockUR+1)这块)
        double towerDR = (building.BlockDR + 1) * blockLength();
        double towerUR = (building.BlockUR + 1) * blockLength();

        //找射程内最近的敌人:先军队,再农民,最后建筑(只挑射程内的,免得箭塔干等)
        const double rangeLimit = towerRange * blockLength();
        int targetSN = -1;
        double bestDistance = 99999999.0;

        //敌人的军队(有细节坐标,直接算准确距离)
        for (unsigned int j = 0; j < info.enemy_armies.size(); j++) {
            double d = fabs(towerDR - info.enemy_armies[j].DR) + fabs(towerUR - info.enemy_armies[j].UR);
            if (d <= rangeLimit && d < bestDistance) {
                bestDistance = d;
                targetSN = info.enemy_armies[j].SN;
            }
        }
        //敌人的农民
        for (unsigned int j = 0; j < info.enemy_farmers.size(); j++) {
            double d = fabs(towerDR - info.enemy_farmers[j].DR) + fabs(towerUR - info.enemy_farmers[j].UR);
            if (d <= rangeLimit && d < bestDistance) {
                bestDistance = d;
                targetSN = info.enemy_farmers[j].SN;
            }
        }
        //军队和农民都没进射程,再考虑打敌人的建筑(建筑只有块坐标,按3*3建筑的中心估算)
        if (targetSN < 0) {
            for (unsigned int j = 0; j < info.enemy_buildings.size(); j++) {
                double d = fabs(towerDR - (info.enemy_buildings[j].BlockDR + 1.5) * blockLength())
                         + fabs(towerUR - (info.enemy_buildings[j].BlockUR + 1.5) * blockLength());
                if (d <= rangeLimit && d < bestDistance) {
                    bestDistance = d;
                    targetSN = info.enemy_buildings[j].SN;
                }
            }
        }
        //射程内一个敌人都没有,这帧就算了(敌人走进射程会自动再选)
        if (targetSN < 0) {
            continue;
        }

        //下指令让箭塔攻击
        HumanAction(building.SN, targetSN);
        rememberOrder(building.SN);
    }
}

/* =====================================================================
 *  祭司开局探路
 *  祭司的寻找由 findPriest 负责,第一波到来前的接管由 strategyMain 负责。
 *  本函数负责:让祭司依次访问指定探路点位。
 *  四个角点:A(12,12) B(12,88) C(88,12) D(88,88),以市镇中心为基准排序。
 *  访问顺序: 第二近 -> 第三近 -> 最近 -> 中心点(50,50),四个点位依次循环直到探路阶段结束。
 *  到达判定:祭司细节坐标连续10帧保持不变。
 *  每个目标点只发送一次 HumanMove 指令。
 *  遇敌处理:视野16格内发现敌方单位(军队/农民/建筑),用细节坐标精确检测,向安全点后撤3秒;后撤结束时若敌人仍在则继续后撤(不切换目标),敌人离开才切换下一目标。
 * ===================================================================== */
void UsrAI::priestExplore(const tagInfo& info)
{
    //找祭司(专门函数负责)
    int priestSN = -1;
    int priestX = 0;
    int priestY = 0;
    double priestDR = 0.0;
    double priestUR = 0.0;
    if (!findPriest(info, priestSN, priestX, priestY, priestDR, priestUR)) {
        return;
    }

    //探路目标点:索引0~3是四角,索引4是中心点
    const int px[5] = {12, 12, 88, 88, 50};
    const int py[5] = {12, 88, 12, 88, 50};

    //距离排序(只做一次):对四个角点按到市镇中心距离排序
    if (!priestVisitSorted) {
        double dist[4];
        int idx[4] = {0, 1, 2, 3};
        for (int i = 0; i < 4; i++) {
            dist[i] = distanceBlock(townX, townY, px[i], py[i]);
        }
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3 - i; j++) {
                if (dist[idx[j]] > dist[idx[j + 1]]) {
                    int tmp = idx[j];
                    idx[j] = idx[j + 1];
                    idx[j + 1] = tmp;
                }
            }
        }
        //访问顺序: 第二近 -> 第三近 -> 最近 -> 中心点(50,50)
        priestVisitOrder[0] = idx[1];   // 第二近
        priestVisitOrder[1] = idx[2];   // 第三近
        priestVisitOrder[2] = idx[0];   // 最近
        priestVisitOrder[3] = 4;        // 中心点(50,50)
        priestVisitSorted = true;
        priestVisitIndex = 0;
        priestMoveSent = false;
        priestStillFrames = 0;
        priestLastDR = priestDR;
        priestLastUR = priestUR;
    }

    //后撤计时:遇敌后向安全点后撤3秒,结束后检测敌人是否还在
    if (priestRetreatFrames > 0) {
        priestRetreatFrames--;
        if (priestRetreatFrames <= 0) {
            //后撤结束:检测敌人是否仍在16格内
            bool enemyStillThere = false;
            const double detectDist = 16.0 * blockLength();
            for (unsigned int i = 0; i < info.enemy_armies.size() && !enemyStillThere; i++) {
                double dx = priestDR - info.enemy_armies[i].DR;
                double dy = priestUR - info.enemy_armies[i].UR;
                if (dx * dx + dy * dy <= detectDist * detectDist) enemyStillThere = true;
            }
            for (unsigned int i = 0; i < info.enemy_farmers.size() && !enemyStillThere; i++) {
                double dx = priestDR - info.enemy_farmers[i].DR;
                double dy = priestUR - info.enemy_farmers[i].UR;
                if (dx * dx + dy * dy <= detectDist * detectDist) enemyStillThere = true;
            }
            for (unsigned int i = 0; i < info.enemy_buildings.size() && !enemyStillThere; i++) {
                double bDR = detailOf(info.enemy_buildings[i].BlockDR);
                double bUR = detailOf(info.enemy_buildings[i].BlockUR);
                double dx = priestDR - bDR;
                double dy = priestUR - bUR;
                if (dx * dx + dy * dy <= detectDist * detectDist) enemyStillThere = true;
            }
            if (enemyStillThere) {
                //敌人还在,继续后撤,不切换目标(不消耗目标点)
                priestRetreatFrames = PRIEST_RETREAT_FRAMES;
            } else {
                //敌人走了,循环切换下一目标
                priestVisitIndex = (priestVisitIndex + 1) % 4;
                priestMoveSent = false;
                priestStillFrames = 0;
            }
        }
        return;    //后撤期间不做到达判定和探路移动指令
    }

    //敌人检测:视野16格内发现敌方单位(军队+农民+建筑),向安全点后撤3秒
    //用细节坐标算距离,精度高,避免块坐标粗粒度导致延迟检测
    bool enemyInSight = false;
    const double detectDist = 16.0 * blockLength();   //16格对应的细节坐标距离
    for (unsigned int i = 0; i < info.enemy_armies.size() && !enemyInSight; i++) {
        double dx = priestDR - info.enemy_armies[i].DR;
        double dy = priestUR - info.enemy_armies[i].UR;
        if (dx * dx + dy * dy <= detectDist * detectDist) {
            enemyInSight = true;
        }
    }
    for (unsigned int i = 0; i < info.enemy_farmers.size() && !enemyInSight; i++) {
        double dx = priestDR - info.enemy_farmers[i].DR;
        double dy = priestUR - info.enemy_farmers[i].UR;
        if (dx * dx + dy * dy <= detectDist * detectDist) {
            enemyInSight = true;
        }
    }
    for (unsigned int i = 0; i < info.enemy_buildings.size() && !enemyInSight; i++) {
        //建筑没有细节坐标,用detailOf把块坐标转成细节坐标
        double bDR = detailOf(info.enemy_buildings[i].BlockDR);
        double bUR = detailOf(info.enemy_buildings[i].BlockUR);
        double dx = priestDR - bDR;
        double dy = priestUR - bUR;
        if (dx * dx + dy * dy <= detectDist * detectDist) {
            enemyInSight = true;
        }
    }
    if (enemyInSight) {
        int safeX = 0, safeY = 0;
        if (findPriestSafeSpot(info, safeX, safeY)) {
            HumanMove(priestSN, detailOf(safeX), detailOf(safeY));
        }
        priestRetreatFrames = PRIEST_RETREAT_FRAMES;
        return;
    }

    //到达判定:连续10帧位置不变(用细节坐标判定,精度高;块坐标太粗会导致移动中误判到达)
    double dx = priestDR - priestLastDR;
    double dy = priestUR - priestLastUR;
    if (dx * dx + dy * dy < 1.0) {   //每帧移动距离小于1个细节单位视为不动(祭司移动时每帧约走2个单位)
        priestStillFrames++;
    } else {
        priestStillFrames = 0;
        priestLastDR = priestDR;
        priestLastUR = priestUR;
    }

    //真正到达:连续10帧不动,切换下一目标
    if (priestStillFrames >= 10 && priestMoveSent) {
        int oldIdx = priestVisitOrder[priestVisitIndex];
        priestVisitIndex = (priestVisitIndex + 1) % 4;   // 循环切换
        priestStillFrames = 0;
        priestMoveSent = false;
    }

    //发送当前目标的移动指令(每个点只发一次)
    if (!priestMoveSent) {
        int tIdx = priestVisitOrder[priestVisitIndex];   //重新取最新目标(到达可能已推进index)
        HumanMove(priestSN, detailOf(px[tIdx]), detailOf(py[tIdx]));
        priestMoveSent = true;
        priestStillFrames = 0;
        priestLastDR = priestDR;
        priestLastUR = priestUR;
    }
}

/* =====================================================================
 *  祭司保护
 *  思路:祭司是胜利的关键,死了就输了,所以要一直躲着。
 *      1. 安全点:有箭塔就去箭塔旁边,没有就去市镇中心旁边
 *      2. 危险半径14格:弓箭手射程5格、复合弓/战车弓7格、投石车10格,
 *         敌人进14格就跑,不能被远程白打
 *      3. 没有敌人就回安全点待着
 *      4. 安全的时候顺手给身边受伤的部队加血
 * ===================================================================== */
void UsrAI::priestBehavior(const tagInfo& info)
{
    //反攻阶段(阶段4)暂未实现,当前只负责保祭司和治疗
    if (gameStage >= 4) {
        return;
    }

    //找祭司
    int priestSN = -1;
    int priestX = 0;
    int priestY = 0;
    double priestDR = 0.0;
    double priestUR = 0.0;
    if (!findPriest(info, priestSN, priestX, priestY, priestDR, priestUR)) {
        priestLastBlood = -1;    //祭司没了,重置血量记录
        return;    //没有祭司(祭司死了游戏就输了)
    }

    //拿祭司当前血量(检测受击用,findPriest不返回血量)
    int priestBlood = 0;
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        if (info.armies[i].SN == priestSN) {
            priestBlood = info.armies[i].Blood;
            break;
        }
    }

    //受击检测:血量比上一帧少,说明正在被打,先记下来(拿到安全点后立刻跑)
    bool underAttack = false;
    if (priestLastBlood >= 0 && priestBlood < priestLastBlood) {
        underAttack = true;
    }
    priestLastBlood = priestBlood;    //更新血量记录

    //找安全点(箭塔旁或市镇中心旁)
    int safeX = 0;
    int safeY = 0;
    if (!findPriestSafeSpot(info, safeX, safeY)) {
        return;
    }

    //受击了:最紧急,立刻往安全点跑(有箭塔和军队保护),3帧就能改一次方向
    if (underAttack) {
        if (canOrder(priestSN, 3)) {
            HumanMove(priestSN, detailOf(safeX), detailOf(safeY));
            rememberOrder(priestSN);
        }
        return;
    }

    //找离祭司最近的敌人(军队优先,再看农民)
    int enemySN = -1;
    double enemyDist = 99999999.0;
    double enemyDR = 0.0;
    double enemyUR = 0.0;
    for (unsigned int i = 0; i < info.enemy_armies.size(); i++) {
        double d = distanceBlock(priestX, priestY, info.enemy_armies[i].BlockDR, info.enemy_armies[i].BlockUR);
        if (d < enemyDist) {
            enemyDist = d;
            enemySN = info.enemy_armies[i].SN;
            enemyDR = info.enemy_armies[i].DR;
            enemyUR = info.enemy_armies[i].UR;
        }
    }
    if (enemySN < 0) {
        for (unsigned int i = 0; i < info.enemy_farmers.size(); i++) {
            double d = distanceBlock(priestX, priestY, info.enemy_farmers[i].BlockDR, info.enemy_farmers[i].BlockUR);
            if (d < enemyDist) {
                enemyDist = d;
                enemySN = info.enemy_farmers[i].SN;
                enemyDR = info.enemy_farmers[i].DR;
                enemyUR = info.enemy_farmers[i].UR;
            }
        }
    }

    //分级响应:
    //16格内=预警,先往安全点靠(还没被打,提前动起来)
    //12格内=危险,紧急逃跑(远离敌人+朝向安全点)
    //逃跑方向:远离所有敌人的威胁中心 + 朝向安全点(箭塔/市镇中心),两个方向合成,
    //这样既拉开距离又不会跑散,始终往有保护的地方靠
    const double warnDist = 16.0;
    const double dangerDist = 12.0;
    if (enemySN >= 0 && enemyDist <= warnDist) {
        if (enemyDist > dangerDist) {
            //12~16格:预警,往安全点靠,10帧改一次方向
            if (canOrder(priestSN, 10)) {
                HumanMove(priestSN, detailOf(safeX), detailOf(safeY));
                rememberOrder(priestSN);
            }
            return;
        }
        //12格内:危险,紧急逃跑
        //计算威胁中心:所有14格内敌人的加权平均位置(越近权重越大),
        //不是只看最近的一个,免得被侧面绕过来的敌人包抄
        double threatDR = 0.0;
        double threatUR = 0.0;
        double weightSum = 0.0;
        for (unsigned int i = 0; i < info.enemy_armies.size(); i++) {
            double d = distanceBlock(priestX, priestY, info.enemy_armies[i].BlockDR, info.enemy_armies[i].BlockUR);
            if (d <= dangerDist) {
                double w = 1.0 / (d + 1.0);
                threatDR += info.enemy_armies[i].DR * w;
                threatUR += info.enemy_armies[i].UR * w;
                weightSum += w;
            }
        }
        for (unsigned int i = 0; i < info.enemy_farmers.size(); i++) {
            double d = distanceBlock(priestX, priestY, info.enemy_farmers[i].BlockDR, info.enemy_farmers[i].BlockUR);
            if (d <= dangerDist) {
                double w = 1.0 / (d + 1.0);
                threatDR += info.enemy_farmers[i].DR * w;
                threatUR += info.enemy_farmers[i].UR * w;
                weightSum += w;
            }
        }
        if (weightSum > 0.001) {
            threatDR /= weightSum;
            threatUR /= weightSum;
        } else {
            threatDR = enemyDR;
            threatUR = enemyUR;
        }

        //远离威胁中心的方向(归一化)
        double awayX = priestDR - threatDR;
        double awayY = priestUR - threatUR;
        double awayLen = sqrt(awayX * awayX + awayY * awayY);
        if (awayLen > 0.001) {
            awayX /= awayLen;
            awayY /= awayLen;
        }

        //朝向安全点的方向(归一化)
        double safeDR = detailOf(safeX);
        double safeUR = detailOf(safeY);
        double toSafeX = safeDR - priestDR;
        double toSafeY = safeUR - priestUR;
        double toSafeLen = sqrt(toSafeX * toSafeX + toSafeY * toSafeY);
        if (toSafeLen > 0.001) {
            toSafeX /= toSafeLen;
            toSafeY /= toSafeLen;
        }

        //合成最终逃跑方向:远离敌人占6成,朝安全点占4成(先保命再靠塔)
        double runDirX = awayX * 0.6 + toSafeX * 0.4;
        double runDirY = awayY * 0.6 + toSafeY * 0.4;
        double runDirLen = sqrt(runDirX * runDirX + runDirY * runDirY);
        if (runDirLen > 0.001) {
            runDirX /= runDirLen;
            runDirY /= runDirLen;
        }

        //逃跑距离:敌人越近跑越远,最远10格,最近6格
        double runDist = 6.0 + (dangerDist - enemyDist) / dangerDist * 4.0;
        double runDR = priestDR + runDirX * runDist * blockLength();
        double runUR = priestUR + runDirY * runDist * blockLength();

        //边界限制
        double maxCoord = 100.0 * blockLength();
        if (runDR < 0) runDR = 0.0;
        if (runUR < 0) runUR = 0.0;
        if (runDR > maxCoord) runDR = maxCoord;
        if (runUR > maxCoord) runUR = maxCoord;

        if (canOrder(priestSN, 5)) {
            HumanMove(priestSN, runDR, runUR);
            rememberOrder(priestSN);
        }
        return;
    }

    //没有敌人威胁:离安全点还远就回去待着
    if (distanceBlock(priestX, priestY, safeX, safeY) > 3.0) {
        if (canOrder(priestSN, 30)) {
            //目标点稍微偏移一格,免得站在建筑正中间
            int targetX = safeX;
            int targetY = safeY;
            if (targetX < 1) targetX = 1;
            if (targetY < 1) targetY = 1;
            if (targetX > 98) targetX = 98;
            if (targetY > 98) targetY = 98;
            HumanMove(priestSN, detailOf(targetX), detailOf(targetY));
            rememberOrder(priestSN);
        }
        return;
    }

    //已经在安全点附近了:给身边受伤的部队加血(祭司会治疗)
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        const tagArmy& army = info.armies[i];
        if (army.SN == priestSN) {
            continue;    //不能给自己加
        }
        if (army.Blood >= army.MaxBlood) {
            continue;    //血满了不用加
        }
        if (distanceBlock(priestX, priestY, army.BlockDR, army.BlockUR) <= 2.0) {
            if (canOrder(priestSN, 25)) {
                HumanAction(priestSN, army.SN);
                rememberOrder(priestSN);
            }
            break;
        }
    }
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        const tagFarmer& farmer = info.farmers[i];
        if (farmer.Blood >= farmer.MaxBlood) {
            continue;
        }
        if (distanceBlock(priestX, priestY, farmer.BlockDR, farmer.BlockUR) <= 2.0) {
            if (canOrder(priestSN, 25)) {
                HumanAction(priestSN, farmer.SN);
                rememberOrder(priestSN);
            }
            break;
        }
    }
}

/* =====================================================================
 *  每帧的主逻辑
 * ===================================================================== */
void UsrAI::strategyMain(const tagInfo& info)
{
    //找市镇中心
    int newTownX = -1, newTownY = -1;
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].Type == BUILDING_CENTER) {
            newTownX = info.buildings[i].BlockDR;
            newTownY = info.buildings[i].BlockUR;
            break;
        }
    }
    if (newTownX < 0) {
        return;    //还没找到市镇中心,先不干活
    }
    townX = newTownX;
    townY = newTownY;

    //新一局检测:农民数量突然大幅减少(>2个)说明是新一局
    //(游戏内重启不重置GameFrame,但单位列表会重新初始化,数量跳回初始值;
    // 探路阶段在第一波攻击前,农民不会被大量杀死,误判概率极低)
    static int lastFarmerCount = -1;
    int currentFarmerCount = (int)info.farmers.size();
    if (lastFarmerCount >= 0 && currentFarmerCount < lastFarmerCount - 2) {
        gameStartFrame = info.GameFrame;
        gameStage = 1;
        //重置祭司探路状态(新一局,避免上一局的探路进度残留导致祭司不再探路)
        priestVisitIndex = -1;
        priestVisitSorted = false;
        priestMoveSent = false;
        priestStillFrames = 0;
        priestLastDR = -1.0;
        priestLastUR = -1.0;
        priestRetreatFrames = 0;
    }
    lastFarmerCount = currentFarmerCount;
    if (gameStartFrame < 0) {
        gameStartFrame = info.GameFrame;
    }
    int relativeFrame = info.GameFrame - gameStartFrame;

    //更新地图信息
    updateMapInfo(info);

    //祭司行为接管:
    //第一波(6000帧)到来前:祭司出去探路,行为完全由priestExplore接管
    //第一波即将到来(5600帧起):结束探路,交给priestBehavior保护代码,返回安全点
    if (relativeFrame < PRIEST_EXPLORE_END_FRAME) {
        priestExplore(info);          // 祭司开局探路
    } else {
        priestBehavior(info);         // 保护祭司(回安全点)
    }


    //按照攻略4阶段划分:
    //阶段1:游戏开始,探路和采集资源(第一波之前)
    //阶段2:防御第一波攻击,升级铜器(第一波到来~铜器升级完成)
    //阶段3:防御第二、三波攻击,规模化造兵
    //阶段4:进攻敌人基地,完成胜利目标
    if (gameStage == 1 && relativeFrame >= 6000) {
        gameStage = 2;    //第一波来了(约4分钟),进入防御第一波阶段
    }
    if (gameStage == 2 && info.civilizationStage == CIVILIZATION_BRONZEAGE) {
        gameStage = 3;    //升到铜器时代了,进入规模化造兵阶段
    }

    //按照顺序做事情(造兵永远比研究科技优先)
    makeVillager(info);      //生产村民
    upgradeAge(info);        //升级时代
    makeArmy(info);          //造士兵(先造兵,科技往后放,不然波次来了没兵)
    researchTech(info);      //研究科技
    buildHouse(info);        //盖房子
    buildExtraTowers(info);   // 额外建造箭塔（在科技解锁后）
    buildSomeBuilding(info, BUILDING_MARKET);      //盖市场
    buildSomeBuilding(info, BUILDING_ARMYCAMP);    //盖兵营
    buildSomeBuilding(info, BUILDING_RANGE);       //盖靶场
    buildSomeBuilding(info, BUILDING_STABLE);      //盖马厩
    buildFarm(info);         //盖农田(食物稳定,不然后期没东西吃)
    if (hasArrowTech) {
        buildSomeBuilding(info, BUILDING_ARROWTOWER);   //有科技了盖箭塔
    }
    if (gameStage >= 3) {
        buildSomeBuilding(info, BUILDING_COLLAGE);      //铜器时代(阶段2+)盖学院
    }
    assignWork(info);        //给村民安排工作
    armyFight(info);         //军队打架
    towerFight(info);        //箭塔自动攻击射程内的敌人

    //如果建筑都盖完了,把专门盖房子的人解放出来
    bool needBuild = false;
    if (countBuilding(info, BUILDING_HOME) < 6) {
        needBuild = true;    //前期至少6间房子
    }
    if (gameStage >= 3 && countBuilding(info, BUILDING_HOME) < 12) {
        needBuild = true;    //铜器(阶段2+)至少12间房子
    }
    if (findBuilding(info, BUILDING_MARKET) < 0) {
        needBuild = true;
    }
    if (findBuilding(info, BUILDING_ARMYCAMP) < 0) {
        needBuild = true;
    }
    if (findBuilding(info, BUILDING_RANGE) < 0) {
        needBuild = true;
    }
    if (findBuilding(info, BUILDING_STABLE) < 0) {
        needBuild = true;
    }
    if (findBuilding(info, BUILDING_GRANARY) < 0) {
        needBuild = true;    //谷仓要留一个,箭塔科技靠它研究
    }
    if (countBuilding(info, BUILDING_FARM) < 2) {
        needBuild = true;    //农田不能少于2块
    }
    if (gameStage == 3 && findBuilding(info, BUILDING_COLLAGE) < 0) {
        needBuild = true;    //阶段3(铜器防守)需要学院
    }
    if (!needBuild) {
        //把空闲的建造工改成没工作,让分配工作的函数重新给他安排
        map<int,int>::iterator it = villagerWork.begin();
        while (it != villagerWork.end()) {
            if (it->second == WORK_BUILD) {
                bool idle = false;
                for (unsigned int i = 0; i < info.farmers.size(); i++) {
                    if (info.farmers[i].SN == it->first && info.farmers[i].NowState == HUMAN_STATE_IDLE) {
                        idle = true;
                        break;
                    }
                }
                if (idle) {
                    it->second = WORK_NONE;
                }
            }
            it++;
        }
    }
}

/* ============================ 入口 ============================ */
void UsrAI::processData()
{
    tagInfo info = getInfo();
    strategyMain(info);
}
