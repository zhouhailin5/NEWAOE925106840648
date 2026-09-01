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
 *      - 安排建筑(房子、市场、兵营、靶场、马厩、箭塔、学院)
 *      - 研究科技
 *      - 给空闲的村民安排工作
 *      - 生产士兵
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

    //前面阶段多砍树,后面阶段要种田和挖金子
    int wantWood = villagerNum / 3;
    if (wantWood < 3) {
        wantWood = 3;
    }
    if (wantWood > 6) {
        wantWood = 6;
    }

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
            //前期:多采浆果,砍树,少挖石头,打猎
            if (numBerry < 3) {
                chooseWork = WORK_BERRY;
            } else if (numStone < 1) {
                chooseWork = WORK_STONE;
            } else if (numHunt < 1) {
                chooseWork = WORK_HUNT;
            } else if (numWood < wantWood) {
                chooseWork = WORK_WOOD;
            } else {
                chooseWork = WORK_WOOD;
            }
        } else {
            //铜器时代:挖石头、打猎、挖金、种田都要
            if (numStone < 1) {
                chooseWork = WORK_STONE;
            } else if (numHunt < 2) {
                chooseWork = WORK_HUNT;
            } else if (numGold < 2) {
                chooseWork = WORK_GOLD;
            } else if (numFarm < 6) {
                chooseWork = WORK_FARM;
            } else if (numWood < 4) {
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
    //看看现在有几间房子,前期盖到4间,后期盖到9间(房子多了人口上限才高)
    int houseNum = countBuilding(info, BUILDING_HOME);
    int wantHouse = 4;
    if (gameStage == 1) {
        wantHouse = 9;
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
 *  研究科技
 * ===================================================================== */
void UsrAI::researchTech(const tagInfo& info)
{
    //谷仓研究箭塔科技(这样村民才能盖箭塔)
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

    //市场研究伐木科技
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

    //铜器时代后:市场研究车轮、金矿、驯养动物
    if (gameStage >= 1) {
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

    //仓库研究工具使用(加攻击)和步兵护甲
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

    //兵营研究战斧升级,铜器后研究阔剑科技
    if (!hasAxeTech && info.Meat >= BUILDING_ARMYCAMP_UPGRADE_CLUBMAN_FOOD) {
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
    if (gameStage >= 1 && !hasBroadTech && info.Meat >= BUILDING_ARMYCAMP_UPGRADE_BROADSWORD_FOOD
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

    //靶场研究复合弓科技
    if (gameStage >= 1 && !hasCompositeTech && info.Meat >= BUILDING_RANGE_UPGRADE_COMPOSITE_BOW_FOOD
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
    //前期生产到18个,铜器后到22个
    int wantVillager = 18;
    if (gameStage >= 1) {
        wantVillager = 22;
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
 * ===================================================================== */
void UsrAI::makeArmy(const tagInfo& info)
{
    //前期专心发展经济,不造兵,升到铜器再开始造兵
    if (gameStage == 0) {
        return;
    }
    //人口满了就不能造了
    if (info.Human_Num + 1 > info.Human_MaxNum) {
        return;
    }
    //先看一共有几个士兵,够了就不造了
    int armyNum = (int)info.armies.size();
    int wantArmy = 14;
    if (gameStage == 2) {
        wantArmy = 18;
    }
    if (armyNum >= wantArmy) {
        return;
    }

    //兵营造棍棒兵(有阔剑科技就造阔剑兵)
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
            if (countArmy(info, AT_CLUBMAN) < 4) {
                if (info.Meat >= BUILDING_ARMYCAMP_CREATE_CLUBMAN_FOOD) {
                    BuildingAction(campSN, BUILDING_ARMYCAMP_CREATE_CLUBMAN);
                    return;
                }
            }
        }
    }

    //靶场造弓箭手(有复合弓科技就造复合弓兵)
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
            if (hasCompositeTech && bowmanNum < 5) {
                if (info.Meat >= BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN_FOOD
                    && info.Gold >= BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN_GOLD) {
                    BuildingAction(rangeSN, BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN);
                    return;
                }
            }
            if (bowmanNum < 3) {
                if (info.Meat >= BUILDING_RANGE_CREATE_BOWMAN_FOOD
                    && info.Wood >= BUILDING_RANGE_CREATE_BOWMAN_WOOD) {
                    BuildingAction(rangeSN, BUILDING_RANGE_CREATE_BOWMAN);
                    return;
                }
            }
        }
    }

    //马厩造侦察骑兵(可以探路)
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

    //学院造方阵兵
    if (gameStage >= 1 && info.Gold >= 80) {
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
                if (countArmy(info, AT_HOPLITE) < 4) {
                    if (info.Meat >= BUILDING_COLLAGE_CREATE_HOPLITE_FOOD
                        && info.Gold >= BUILDING_COLLAGE_CREATE_HOPLITE_GOLD) {
                        BuildingAction(collageSN, BUILDING_COLLAGE_CREATE_HOPLITE);
                    }
                }
            }
        }
    }
}

/* =====================================================================
 *  指挥军队攻击看到的敌人
 * ===================================================================== */
void UsrAI::armyFight(const tagInfo& info)
{
    //看看有没有能看到的敌人
    bool enemySeen = false;
    if (!info.enemy_armies.empty() || !info.enemy_farmers.empty()) {
        enemySeen = true;
    }
    if (!enemySeen) {
        return;
    }
    //找一个最近的敌人来打
    int enemySN = -1;
    double bestDistance = 99999999.0;
    for (unsigned int i = 0; i < info.enemy_armies.size(); i++) {
        int ex = info.enemy_armies[i].BlockDR;
        int ey = info.enemy_armies[i].BlockUR;
        for (unsigned int j = 0; j < info.armies.size(); j++) {
            if (info.armies[j].Sort == AT_PRIEST) {
                continue;
            }
            double d = distanceBlock(ex, ey, info.armies[j].BlockDR, info.armies[j].BlockUR);
            if (d < bestDistance) {
                bestDistance = d;
                enemySN = info.enemy_armies[i].SN;
            }
        }
    }
    if (enemySN < 0 && !info.enemy_farmers.empty()) {
        enemySN = info.enemy_farmers[0].SN;
    }
    if (enemySN < 0) {
        return;
    }
    //让所有空闲的士兵去打这个敌人
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        const tagArmy& army = info.armies[i];
        if (army.Sort == AT_PRIEST) {
            continue;    //祭司不打架,他是用来转化的
        }
        if (army.NowState == HUMAN_STATE_IDLE || army.NowState == HUMAN_STATE_WALKING) {
            if (!canOrder(army.SN, 8)) {
                continue;
            }
            HumanAction(army.SN, enemySN);
            rememberOrder(army.SN);
        }
    }
}

/* =====================================================================
 *  防守波次攻击
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

    //第一波的时候,村民如果受伤了,就让他躲回市镇中心旁边
    if (waveFlag == 1) {
        bool enemyAround = false;
        if (!info.enemy_armies.empty()) {
            enemyAround = true;
        }
        if (enemyAround) {
            for (unsigned int i = 0; i < info.farmers.size(); i++) {
                const tagFarmer& farmer = info.farmers[i];
                if (farmer.FarmerSort != 0) {
                    continue;
                }
                //受伤了(血不满)才躲
                if (farmer.Blood < farmer.MaxBlood && farmer.NowState != HUMAN_STATE_IDLE) {
                    if (canOrder(farmer.SN, 30)) {
                        HumanMove(farmer.SN, detailOf(townX + 1), detailOf(townY + 1));
                        rememberOrder(farmer.SN);
                    }
                }
            }
        }
    }
}

/* =====================================================================
 *  派一个村民去探路,找金矿和敌方基地
 * ===================================================================== */
void UsrAI::exploreMap(const tagInfo& info)
{
    //到了铜器时代就不用村民探路了,改用侦察骑兵
    if (gameStage != 0) {
        if (exploreSN >= 0) {
            //把探路的村民解放出来,让他去干活
            map<int,int>::iterator it = villagerWork.find(exploreSN);
            if (it != villagerWork.end()) {
                it->second = WORK_NONE;
            }
            exploreSN = -1;
        }
        return;
    }
    //如果已经找到金矿了,就不用探了
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
    //如果看到敌方建筑了,也不用探了
    if (!info.enemy_buildings.empty()) {
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
    //让他往远处走,走完一个方向换一个方向
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        if (info.farmers[i].SN != exploreSN) {
            continue;
        }
        if (info.farmers[i].NowState != HUMAN_STATE_IDLE) {
            return;
        }
        if (!canOrder(exploreSN, 40)) {
            return;
        }
        //四个方向:右下、左上、右上、左下(块坐标)
        int targetX = townX;
        int targetY = townY;
        if (exploreIndex == 0) {
            targetX = 80;
            targetY = 80;
        } else if (exploreIndex == 1) {
            targetX = 10;
            targetY = 80;
        } else if (exploreIndex == 2) {
            targetX = 80;
            targetY = 10;
        } else {
            targetX = 10;
            targetY = 10;
        }
        exploreIndex++;
        if (exploreIndex > 3) {
            exploreIndex = 0;
        }
        HumanMove(exploreSN, detailOf(targetX), detailOf(targetY));
        rememberOrder(exploreSN);
        break;
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
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        if (info.armies[i].Sort == AT_PRIEST) {
            priestSN = info.armies[i].SN;
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
    //看看附近还有没有敌人
    bool enemyNear = false;
    for (unsigned int i = 0; i < info.enemy_armies.size(); i++) {
        if (abs(info.enemy_armies[i].BlockDR - enemyBaseX) <= 12
            && abs(info.enemy_armies[i].BlockUR - enemyBaseY) <= 12) {
            enemyNear = true;
            break;
        }
    }
    //指挥祭司
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        if (info.armies[i].SN != priestSN) {
            continue;
        }
        if (enemyNear) {
            //还有敌人,祭司先躲远一点,别被打死了
            if (info.armies[i].NowState == HUMAN_STATE_IDLE && canOrder(priestSN, 40)) {
                int rx = enemyBaseX - 4;
                if (rx < 1) {
                    rx = 1;
                }
                int ry = enemyBaseY - 4;
                if (ry < 1) {
                    ry = 1;
                }
                HumanMove(priestSN, detailOf(rx), detailOf(ry));
                rememberOrder(priestSN);
            }
        } else {
            //没有敌人了,让祭司去转化武器工程厂,转化成功就赢了
            if (info.armies[i].NowState == HUMAN_STATE_IDLE && canOrder(priestSN, 10)) {
                HumanAction(priestSN, siegeSN);
                rememberOrder(priestSN);
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

    //判断阶段:升到铜器时代就进入阶段1
    if (gameStage == 0 && info.civilizationStage == CIVILIZATION_BRONZEAGE) {
        gameStage = 1;
    }
    //第三波打完之后(大概16分钟)开始反攻
    if (gameStage == 1 && waveFlag >= 3 && info.GameFrame >= 24000) {
        gameStage = 2;
    }

    //按照顺序做事情
    makeVillager(info);      //生产村民
    upgradeAge(info);        //升级时代
    researchTech(info);      //研究科技
    buildHouse(info);        //盖房子
    buildSomeBuilding(info, BUILDING_MARKET);      //盖市场
    buildSomeBuilding(info, BUILDING_ARMYCAMP);    //盖兵营
    buildSomeBuilding(info, BUILDING_RANGE);       //盖靶场
    buildSomeBuilding(info, BUILDING_STABLE);      //盖马厩
    if (hasArrowTech) {
        buildSomeBuilding(info, BUILDING_ARROWTOWER);   //有科技了盖箭塔
    }
    if (gameStage >= 1) {
        buildSomeBuilding(info, BUILDING_COLLAGE);      //铜器时代盖学院
    }
    assignWork(info);        //给村民安排工作
    exploreMap(info);        //探路
    makeArmy(info);          //造士兵
    armyFight(info);         //军队打架
    defendBase(info);        //防守
    if (gameStage == 2) {
        attackEnemyBase(info);   //反攻
    }

    //如果建筑都盖完了,把专门盖房子的人解放出来
    bool needBuild = false;
    if (countBuilding(info, BUILDING_HOME) < 4) {
        needBuild = true;
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
