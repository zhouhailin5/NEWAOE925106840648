#include "UsrAI.h"
#include<set>
#include <iostream>
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
 *  3. 游戏分三个阶段:
 *      阶段0:前期发展,攒资源,升到铜器时代
 *      阶段1:铜器时代,防守敌人三波进攻,同时造兵
 *      阶段2:反攻,进攻敌方基地,用祭司转化武器工程厂获胜
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
#define WORK_EXPLORE 8   //探路

/* ---------- 全局变量(跨帧保存的) ---------- */
static int gameStage = 0;                 //当前游戏阶段,0是前期,1是铜器,2是反攻
static int townX = -1;                    //市镇中心的块坐标X
static int townY = -1;                    //市镇中心的块坐标Y
static int waveFlag = 0;                  //当前打到第几波(0还没打,1第一波,2第二波,3第三波)
static int enemyBaseX = -1;               //敌方基地的块坐标X(探出来之后才知道)
static int enemyBaseY = -1;               //敌方基地的块坐标Y

//用来记录村民现在在干什么活的表
static map<int,int> villagerWork;         //村民编号 -> 工种
static map<int,int> villagerTarget;       //村民编号 -> 他正在干活的对象的编号
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

//探路用的
static int exploreSN = -1;                //派出去探路的村民编号
static int exploreIndex = 0;              //探路方向轮换用的
static int scanIndex = 0;                 //反攻时侦察兵方向轮换用的

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

//细节坐标转块坐标
int UsrAI::blockOf(double detail)
{
    int b = (int)(detail / blockLength());
    return b;
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

//找到距离(x,y)最近的某种资源的编号,找不到返回-1
int UsrAI::findResource(const tagInfo& info, int resType, int x, int y)
{
    int bestSN = -1;
    double bestDistance = 99999999.0;
    for (unsigned int i = 0; i < info.resources.size(); i++) {
        if (info.resources[i].Type != resType) {
            continue;    //不是这种资源,跳过
        }
        if (info.resources[i].Cnt <= 0) {
            continue;    //资源采完了,跳过
        }
        int rx = info.resources[i].BlockDR;
        int ry = info.resources[i].BlockUR;
        double d = distanceBlock(x, y, rx, ry);
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
    //检查这个位置有没有被别的建筑占着
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        int bX = info.buildings[i].BlockDR;
        int bY = info.buildings[i].BlockUR;
        int bSize = 3;
        if (info.buildings[i].Type == BUILDING_HOME || info.buildings[i].Type == BUILDING_ARROWTOWER) {
            bSize = 2;    //房子和箭塔是2*2的
        }
        //判断两个方块有没有重叠
        if (bX < x + size && bX + bSize > x && bY < y + size && bY + bSize > y) {
            return false;
        }
    }
    //检查有没有被敌人的建筑占着
    for (unsigned int i = 0; i < info.enemy_buildings.size(); i++) {
        int bX = info.enemy_buildings[i].BlockDR;
        int bY = info.enemy_buildings[i].BlockUR;
        int bSize = 3;
        if (info.enemy_buildings[i].Type == BUILDING_HOME || info.enemy_buildings[i].Type == BUILDING_ARROWTOWER) {
            bSize = 2;
        }
        if (bX < x + size && bX + bSize > x && bY < y + size && bY + bSize > y) {
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
    //从半径3开始找,找到半径18,一圈一圈找
    for (int r = 3; r <= 18; r++) {
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
        //盖房子的人由盖房子的函数管,探路的人由探路的函数管,这里都不管
        map<int,int>::iterator workIt = villagerWork.find(farmer.SN);
        int nowWork = WORK_NONE;
        if (workIt != villagerWork.end()) {
            nowWork = workIt->second;
        }
        if (nowWork == WORK_BUILD || nowWork == WORK_EXPLORE) {
            continue;
        }
        //别每帧都给他下指令,隔几帧再下
        if (!canOrder(farmer.SN, 10)) {
            continue;
        }

        //根据现在的缺人情况,决定让他干什么活
        int chooseWork = WORK_WOOD;    //默认去砍树
        if (gameStage == 0) {
            //前期:浆果、木头为主,石头金矿各1个,1个打猎
            if (numBerry < 3) {
                chooseWork = WORK_BERRY;
            } else if (numStone < 1) {
                chooseWork = WORK_STONE;
            } else if (numGold < 1) {
                chooseWork = WORK_GOLD;
            } else if (numHunt < 1) {
                chooseWork = WORK_HUNT;
            } else if (numWood < wantWood) {
                chooseWork = WORK_WOOD;
            } else {
                chooseWork = WORK_WOOD;
            }
        } else {
            //铜器时代:石头、金子都要多挖,种田补食物
            if (numStone < 2) {
                chooseWork = WORK_STONE;
            } else if (numGold < 3) {
                chooseWork = WORK_GOLD;
            } else if (numHunt < 1) {
                chooseWork = WORK_HUNT;
            } else if (numFarm < 3) {
                chooseWork = WORK_FARM;
            } else if (numWood < wantWood) {
                chooseWork = WORK_WOOD;
            } else {
                chooseWork = WORK_FARM;
            }
        }

        //看看这种资源还有没有,没有的话就去砍树
        int targetSN = -1;
        if (chooseWork == WORK_BERRY) {
            targetSN = findResource(info, RESOURCE_BUSH, farmer.BlockDR, farmer.BlockUR);
        } else if (chooseWork == WORK_STONE) {
            targetSN = findResource(info, RESOURCE_STONE, farmer.BlockDR, farmer.BlockUR);
        } else if (chooseWork == WORK_HUNT) {
            targetSN = findResource(info, RESOURCE_GAZELLE, farmer.BlockDR, farmer.BlockUR);
        } else if (chooseWork == WORK_GOLD) {
            targetSN = findResource(info, RESOURCE_GOLD, farmer.BlockDR, farmer.BlockUR);
        } else if (chooseWork == WORK_FARM) {
            //种田:找一个还有食物的农田
            for (unsigned int j = 0; j < info.buildings.size(); j++) {
                if (info.buildings[j].Type == BUILDING_FARM && info.buildings[j].Percent >= 100
                    && info.buildings[j].Cnt > 0) {
                    targetSN = info.buildings[j].SN;
                    break;
                }
            }
        } else {
            targetSN = findResource(info, RESOURCE_TREE, farmer.BlockDR, farmer.BlockUR);
        }

        //没找到目标资源,就改成砍树
        if (targetSN < 0) {
            if (chooseWork != WORK_WOOD) {
                chooseWork = WORK_WOOD;
                targetSN = findResource(info, RESOURCE_TREE, farmer.BlockDR, farmer.BlockUR);
            }
        }
        if (targetSN < 0) {
            continue;    //连树都没有,那就没办法了
        }

        //下指令让他去干活
        HumanAction(farmer.SN, targetSN);
        rememberOrder(farmer.SN);
        villagerWork[farmer.SN] = chooseWork;
        villagerTarget[farmer.SN] = targetSN;
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
    if (gameStage >= 1) {
        wantHouse = 12;
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
    if (gameStage >= 1) {
        wantFarm = 6;
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

    // 目标数量：4个（初始1个 + 额外3个），箭塔多了守家才稳
    const int TARGET_TOWERS = 4;
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
    if (gameStage >= 1 && !hasTowerUpTech
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
    if (gameStage >= 1) {
        wantVillager = 16;
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
    if (gameStage == 2) {
        wantArmy = 26;                //反攻:26个
    }
    if ((int)info.armies.size() >= wantArmy) {
        return;
    }

    //========== 学院:方阵兵(铜器主力,血厚攻高,顶在第一排) ==========
    if (gameStage >= 1) {
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
    if (gameStage >= 1) {
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
 *      反攻阶段交给attackEnemyBase指挥。
 * ===================================================================== */
void UsrAI::armyFight(const tagInfo& info)
{
    //反攻阶段不管防守,统一由attackEnemyBase带队
    if (gameStage == 2) {
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
    if (enemyBaseX >= 0) {
        //知道敌方基地在哪了,就往敌人那个方向偏,军队挡在祭司和敌人中间
        if (enemyBaseX > safeX) {
            rallyX = safeX + 2;
        } else {
            rallyX = safeX - 2;
        }
        if (enemyBaseY > safeY) {
            rallyY = safeY + 2;
        } else {
            rallyY = safeY - 2;
        }
    }
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
 *  防守波次攻击
 *  说明:敌人的波次会专门盯着农民杀(杀够一定数量就撤),所以只要有
 *      敌人摸到基地附近,就把所有村民撤到市镇中心后面躲起来,
 *      让箭塔和士兵去挡。农民活着,经济才不会崩。
 * ===================================================================== */
void UsrAI::defendBase(const tagInfo& info)
{
    //判断现在打到第几波了(游戏时间,一秒钟25帧)
    if (waveFlag < 1 && info.GameFrame >= 6000) {
        waveFlag = 1;    //第一波来了,大概4分钟的时候
    }
    if (waveFlag < 2 && info.GameFrame >= 13500) {
        waveFlag = 2;    //第二波来了,大概9分钟的时候
    }
    if (waveFlag < 3 && info.GameFrame >= 21000) {
        waveFlag = 3;    //第三波来了,大概14分钟的时候
    }

    //看看有没有敌人摸到基地附近(以市镇中心为圆心)
    bool enemyNearBase = false;
    for (unsigned int i = 0; i < info.enemy_armies.size(); i++) {
        if (distanceBlock(info.enemy_armies[i].BlockDR, info.enemy_armies[i].BlockUR, townX, townY) <= 14.0) {
            enemyNearBase = true;
            break;
        }
    }
    if (!enemyNearBase) {
        for (unsigned int i = 0; i < info.enemy_farmers.size(); i++) {
            if (distanceBlock(info.enemy_farmers[i].BlockDR, info.enemy_farmers[i].BlockUR, townX, townY) <= 14.0) {
                enemyNearBase = true;
                break;
            }
        }
    }
    if (!enemyNearBase) {
        return;    //基地附近没有敌人,村民安心干活
    }

    //有敌人靠近,把在外面干活的村民都叫回家(已经在家附近的就不用动)
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        const tagFarmer& farmer = info.farmers[i];
        if (farmer.FarmerSort != 0) {
            continue;    //渔船、运输船不管
        }
        //已经空闲(在家待着)或者已经离市镇中心很近了,就不管
        if (farmer.NowState == HUMAN_STATE_IDLE) {
            continue;
        }
        if (distanceBlock(farmer.BlockDR, farmer.BlockUR, townX, townY) <= 3.0) {
            continue;
        }
        if (canOrder(farmer.SN, 20)) {
            HumanMove(farmer.SN, detailOf(townX + 2), detailOf(townY + 2));
            rememberOrder(farmer.SN);
        }
    }
}

/* =====================================================================
 *  探路
 *  阶段0:派一个村民满地图找金矿(8个方向轮着走)
 *  阶段1:村民解放,改用跑得快的侦察骑兵找敌方基地
 *  另外:只要看到敌方建筑,就顺手把敌方基地的位置记下来,反攻要用
 * ===================================================================== */
void UsrAI::exploreMap(const tagInfo& info)
{
    //只要看到敌方建筑,就记下敌方基地的位置(房子不算,别的建筑更靠谱)
    if (enemyBaseX < 0) {
        for (unsigned int i = 0; i < info.enemy_buildings.size(); i++) {
            if (info.enemy_buildings[i].Type != BUILDING_HOME) {
                enemyBaseX = info.enemy_buildings[i].BlockDR;
                enemyBaseY = info.enemy_buildings[i].BlockUR;
                break;
            }
        }
    }

    if (gameStage == 0) {
        //========== 阶段0:村民找金矿 ==========
        //如果已经找到金矿了,就把他解放出来回去干活
        int goldSN = findResource(info, RESOURCE_GOLD, townX, townY);
        if (goldSN >= 0) {
            if (exploreSN >= 0) {
                map<int,int>::iterator it = villagerWork.find(exploreSN);
                if (it != villagerWork.end()) {
                    it->second = WORK_NONE;
                }
                exploreSN = -1;
            }
            return;
        }
        //找一个空闲村民去探路
        if (exploreSN < 0) {
            for (unsigned int i = 0; i < info.farmers.size(); i++) {
                const tagFarmer& farmer = info.farmers[i];
                if (farmer.FarmerSort != 0) {
                    continue;
                }
                if (farmer.NowState == HUMAN_STATE_IDLE) {
                    exploreSN = farmer.SN;
                    villagerWork[farmer.SN] = WORK_EXPLORE;
                    break;
                }
            }
        }
        if (exploreSN < 0) {
            return;
        }
        //看看这个村民还活着吗
        bool alive = false;
        for (unsigned int i = 0; i < info.farmers.size(); i++) {
            if (info.farmers[i].SN == exploreSN) {
                alive = true;
                break;
            }
        }
        if (!alive) {
            exploreSN = -1;
            return;
        }
        //让他往远处走,8个方向轮着换(四个角+四条边的中间),把地图多扫几遍
        for (unsigned int i = 0; i < info.farmers.size(); i++) {
            if (info.farmers[i].SN != exploreSN) {
                continue;
            }
            if (info.farmers[i].NowState != HUMAN_STATE_IDLE) {
                return;
            }
            if (!canOrder(exploreSN, 20)) {
                return;
            }
            int targetX = townX;
            int targetY = townY;
            if (exploreIndex == 0) {
                targetX = 85;
                targetY = 85;
            } else if (exploreIndex == 1) {
                targetX = 10;
                targetY = 85;
            } else if (exploreIndex == 2) {
                targetX = 85;
                targetY = 10;
            } else if (exploreIndex == 3) {
                targetX = 10;
                targetY = 10;
            } else if (exploreIndex == 4) {
                targetX = 85;
                targetY = 50;
            } else if (exploreIndex == 5) {
                targetX = 10;
                targetY = 50;
            } else if (exploreIndex == 6) {
                targetX = 50;
                targetY = 85;
            } else {
                targetX = 50;
                targetY = 10;
            }
            exploreIndex++;
            if (exploreIndex > 7) {
                exploreIndex = 0;
            }
            HumanMove(exploreSN, detailOf(targetX), detailOf(targetY));
            rememberOrder(exploreSN);
            break;
        }
        return;
    }

    //========== 阶段1及以上:村民解放,改用侦察骑兵 ==========
    if (exploreSN >= 0) {
        //把探路的村民解放出来,让他去干活
        map<int,int>::iterator it = villagerWork.find(exploreSN);
        if (it != villagerWork.end()) {
            it->second = WORK_NONE;
        }
        exploreSN = -1;
    }
    //已经知道敌方基地在哪了,不用再探
    if (enemyBaseX >= 0) {
        return;
    }
    //找一个空闲的侦察骑兵去扫图(跑得快,一会儿就能找到敌方基地)
    int scoutSN = -1;
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        if (info.armies[i].Sort == AT_SCOUT) {
            scoutSN = info.armies[i].SN;
            break;
        }
    }
    if (scoutSN < 0) {
        return;    //还没有侦察兵,等兵营造出来再说
    }
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        if (info.armies[i].SN != scoutSN) {
            continue;
        }
        if (info.armies[i].NowState != HUMAN_STATE_IDLE) {
            return;
        }
        if (!canOrder(scoutSN, 25)) {
            return;
        }
        //四个角轮着去,看到敌方建筑就停了(上面已经记下基地位置)
        int targetX = 85;
        int targetY = 85;
        if (scanIndex == 0) {
            targetX = 85;
            targetY = 20;
        } else if (scanIndex == 1) {
            targetX = 20;
            targetY = 85;
        } else if (scanIndex == 2) {
            targetX = 85;
            targetY = 85;
        } else {
            targetX = 50;
            targetY = 50;
        }
        scanIndex++;
        if (scanIndex > 3) {
            scanIndex = 0;
        }
        HumanMove(scoutSN, detailOf(targetX), detailOf(targetY));
        rememberOrder(scoutSN);
        break;
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
    //反攻阶段交给attackEnemyBase管
    if (gameStage >= 2) {
        return;
    }

    //找祭司
    int priestSN = -1;
    int priestX = 0;
    int priestY = 0;
    double priestDR = 0.0;
    double priestUR = 0.0;
    if (!findPriest(info, priestSN, priestX, priestY, priestDR, priestUR)) {
        return;    //没有祭司(祭司死了游戏就输了)
    }

    //找安全点(箭塔旁或市镇中心旁)
    int safeX = 0;
    int safeY = 0;
    if (!findPriestSafeSpot(info, safeX, safeY)) {
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

    //敌人离祭司太近(14格以内)就要跑,往远离敌人的方向跑
    const double dangerDist = 14.0;
    if (enemySN >= 0 && enemyDist <= dangerDist) {
        double dx = priestDR - enemyDR;
        double dy = priestUR - enemyUR;
        double len = sqrt(dx * dx + dy * dy);
        if (len > 0.001) {
            double runDR = priestDR + (dx / len) * 8.0 * blockLength();
            double runUR = priestUR + (dy / len) * 8.0 * blockLength();
            //边界限制
            double maxCoord = 100.0 * blockLength();
            if (runDR < 0) runDR = 0.0;
            if (runUR < 0) runUR = 0.0;
            if (runDR > maxCoord) runDR = maxCoord;
            if (runUR > maxCoord) runUR = maxCoord;
            if (canOrder(priestSN, 15)) {
                HumanMove(priestSN, runDR, runUR);
                rememberOrder(priestSN);
            }
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
 *  反攻:派军队去敌方基地,最后用祭司转化武器工程厂
 * ===================================================================== */
void UsrAI::attackEnemyBase(const tagInfo& info)
{
    //没到反攻阶段就返回
    if (gameStage != 2) {
        return;
    }
    //如果还不知道敌方基地在哪,先派侦察骑兵(或者随便一个士兵)去找
    if (enemyBaseX < 0) {
        int scoutSN = -1;
        for (unsigned int i = 0; i < info.armies.size(); i++) {
            if (info.armies[i].Sort == AT_SCOUT) {
                scoutSN = info.armies[i].SN;
                break;
            }
        }
        if (scoutSN < 0) {
            for (unsigned int i = 0; i < info.armies.size(); i++) {
                if (info.armies[i].Sort != AT_PRIEST) {
                    scoutSN = info.armies[i].SN;
                    break;
                }
            }
        }
        if (scoutSN < 0) {
            return;
        }
        //让侦察兵往地图四个方向找
        for (unsigned int i = 0; i < info.armies.size(); i++) {
            if (info.armies[i].SN != scoutSN) {
                continue;
            }
            if (info.armies[i].NowState != HUMAN_STATE_IDLE) {
                break;
            }
            if (!canOrder(scoutSN, 40)) {
                break;
            }
            int targetX = 80;
            int targetY = 80;
            if (scanIndex == 0) {
                targetX = 80;
                targetY = 20;
            } else if (scanIndex == 1) {
                targetX = 20;
                targetY = 80;
            } else if (scanIndex == 2) {
                targetX = 80;
                targetY = 80;
            } else {
                targetX = 50;
                targetY = 50;
            }
            scanIndex++;
            if (scanIndex > 3) {
                scanIndex = 0;
            }
            HumanMove(scoutSN, detailOf(targetX), detailOf(targetY));
            rememberOrder(scoutSN);
            break;
        }
        //看看有没有看到敌方建筑,看到了就记住敌方基地的位置
        for (unsigned int i = 0; i < info.enemy_buildings.size(); i++) {
            if (info.enemy_buildings[i].Type == BUILDING_SIEGE
                || info.enemy_buildings[i].Type == BUILDING_ARROWTOWER) {
                enemyBaseX = info.enemy_buildings[i].BlockDR;
                enemyBaseY = info.enemy_buildings[i].BlockUR;
                break;
            }
        }
        return;
    }

    //知道敌方基地在哪了,让主力军队走过去
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        const tagArmy& army = info.armies[i];
        if (army.Sort == AT_PRIEST) {
            continue;
        }
        //算一下离敌方基地多远
        double dist = distanceBlock(army.BlockDR, army.BlockUR, enemyBaseX, enemyBaseY);
        if (dist > 6) {
            //还很远,先走过去
            if (army.NowState == HUMAN_STATE_IDLE && canOrder(army.SN, 20)) {
                HumanMove(army.SN, detailOf(enemyBaseX), detailOf(enemyBaseY));
                rememberOrder(army.SN);
            }
        } else {
            //已经到附近了,打看到的敌人
            if (army.NowState == HUMAN_STATE_IDLE || army.NowState == HUMAN_STATE_WALKING) {
                if (canOrder(army.SN, 8)) {
                    int enemySN = -1;
                    double bestDistance = 99999999.0;
                    for (unsigned int j = 0; j < info.enemy_armies.size(); j++) {
                        double d = distanceBlock(army.BlockDR, army.BlockUR,
                                                 info.enemy_armies[j].BlockDR, info.enemy_armies[j].BlockUR);
                        if (d < bestDistance) {
                            bestDistance = d;
                            enemySN = info.enemy_armies[j].SN;
                        }
                    }
                    if (enemySN < 0 && !info.enemy_buildings.empty()) {
                        //没有敌人了就打建筑(不打武器工程厂,那个要留着转化)
                        for (unsigned int j = 0; j < info.enemy_buildings.size(); j++) {
                            if (info.enemy_buildings[j].Type != BUILDING_SIEGE) {
                                enemySN = info.enemy_buildings[j].SN;
                                break;
                            }
                        }
                    }
                    if (enemySN >= 0) {
                        HumanAction(army.SN, enemySN);
                        rememberOrder(army.SN);
                    }
                }
            }
        }
    }

    //找祭司,让他去转化武器工程厂
    int priestSN = -1;
    int priestX = 0;
    int priestY = 0;
    double priestDR = 0.0;
    double priestUR = 0.0;
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        if (info.armies[i].Sort == AT_PRIEST) {
            priestSN = info.armies[i].SN;
            priestX = info.armies[i].BlockDR;
            priestY = info.armies[i].BlockUR;
            priestDR = info.armies[i].DR;
            priestUR = info.armies[i].UR;
            break;
        }
    }
    if (priestSN < 0) {
        return;    //祭司没了游戏就输了,这里防御一下
    }
    //找敌方武器工程厂
    int siegeSN = -1;
    for (unsigned int i = 0; i < info.enemy_buildings.size(); i++) {
        if (info.enemy_buildings[i].Type == BUILDING_SIEGE) {
            siegeSN = info.enemy_buildings[i].SN;
            break;
        }
    }
    if (siegeSN < 0) {
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
    //找武器工程厂的块坐标(算距离用)
    int siegeX = 0;
    int siegeY = 0;
    for (unsigned int i = 0; i < info.enemy_buildings.size(); i++) {
        if (info.enemy_buildings[i].SN == siegeSN) {
            siegeX = info.enemy_buildings[i].BlockDR;
            siegeY = info.enemy_buildings[i].BlockUR;
            break;
        }
    }

    //指挥祭司:敌人靠近就躲,没有敌人就尽快跟上去转化武器工程厂
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        if (info.armies[i].SN != priestSN) {
            continue;
        }
        if (enemySN >= 0 && enemyDist <= 15.0) {
            //有敌人靠近,祭司先躲远一点(往远离敌人的方向跑),别被打死了
            if (info.armies[i].NowState == HUMAN_STATE_IDLE && canOrder(priestSN, 40)) {
                double dx = priestDR - enemyDR;
                double dy = priestUR - enemyUR;
                double len = sqrt(dx * dx + dy * dy);
                if (len > 0.001) {
                    double runDR = priestDR + (dx / len) * 6.0 * blockLength();
                    double runUR = priestUR + (dy / len) * 6.0 * blockLength();
                    double maxCoord = 100.0 * blockLength();
                    if (runDR < 0) runDR = 0.0;
                    if (runUR < 0) runUR = 0.0;
                    if (runDR > maxCoord) runDR = maxCoord;
                    if (runUR > maxCoord) runUR = maxCoord;
                    HumanMove(priestSN, runDR, runUR);
                    rememberOrder(priestSN);
                }
            }
        } else {
            //没有敌人靠近,让祭司去找武器工程厂转化(转化成功就赢了)
            if (info.armies[i].NowState == HUMAN_STATE_IDLE && canOrder(priestSN, 10)) {
                //离工厂还远就先走过去(别掉队,免得敌人突然出现没人保护)
                if (distanceBlock(priestX, priestY, siegeX, siegeY) > 4.0) {
                    int goX = siegeX - 2;
                    int goY = siegeY - 2;
                    if (goX < 1) goX = 1;
                    if (goY < 1) goY = 1;
                    if (goX > 98) goX = 98;
                    if (goY > 98) goY = 98;
                    HumanMove(priestSN, detailOf(goX), detailOf(goY));
                    rememberOrder(priestSN);
                } else {
                    //到旁边了,开始转化
                    HumanAction(priestSN, siegeSN);
                    rememberOrder(priestSN);
                }
            }
        }
        break;
    }
}

/* =====================================================================
 *  每帧的主逻辑
 * ===================================================================== */
void UsrAI::strategyMain(const tagInfo& info)
{
    //先记录市镇中心的位置
    if (townX < 0) {
        for (unsigned int i = 0; i < info.buildings.size(); i++) {
            if (info.buildings[i].Type == BUILDING_CENTER) {
                townX = info.buildings[i].BlockDR;
                townY = info.buildings[i].BlockUR;
                break;
            }
        }
    }
    if (townX < 0) {
        return;    //还没找到市镇中心,先不干活
    }

    //更新地图信息
    updateMapInfo(info);

    priestBehavior(info); // 保护祭司


    //判断阶段:升到铜器时代就进入阶段1
    if (gameStage == 0 && info.civilizationStage == CIVILIZATION_BRONZEAGE) {
        gameStage = 1;
    }
    //第三波打完之后(大概16分钟)开始反攻
    if (gameStage == 1 && waveFlag >= 3 && info.GameFrame >= 24000) {
        gameStage = 2;
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
    if (gameStage >= 1) {
        buildSomeBuilding(info, BUILDING_COLLAGE);      //铜器时代盖学院
    }
    assignWork(info);        //给村民安排工作
    exploreMap(info);        //探路
    armyFight(info);         //军队打架
    towerFight(info);        //箭塔自动攻击射程内的敌人
    defendBase(info);        //防守
    if (gameStage == 2) {
        attackEnemyBase(info);   //反攻
    }

    //如果建筑都盖完了,把专门盖房子的人解放出来
    bool needBuild = false;
    if (countBuilding(info, BUILDING_HOME) < 6) {
        needBuild = true;    //前期至少6间房子
    }
    if (gameStage >= 1 && countBuilding(info, BUILDING_HOME) < 12) {
        needBuild = true;    //铜器至少12间房子
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
    if (gameStage == 1 && findBuilding(info, BUILDING_COLLAGE) < 0) {
        needBuild = true;
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
