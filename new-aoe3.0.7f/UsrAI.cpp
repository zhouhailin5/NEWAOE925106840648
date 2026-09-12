#include "UsrAI.h"
#include<set>
#include<vector>
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
 *  1. 用全局变量记录村民当前工种,一张表存村民编号到工种的映射
 *  2. 每帧获取游戏信息后,依次安排村民生产、建筑、科技、工作、士兵、探路和防守
 *  3. 游戏分四个阶段:
 *      阶段1:游戏开始,探路和采集资源,第一波之前
 *      阶段2:防御第一波攻击,升级铜器,第一波到铜器升级完成
 *      阶段3:防御第二、三波攻击,规模化造兵
 *      阶段4:进攻敌人基地,完成胜利目标
 * ===================================================================== */

/* ---------- 工种编号 ---------- */
#define WORK_NONE    0   //没有工作
#define WORK_BERRY   1   //采浆果
#define WORK_WOOD    2   //砍树
#define WORK_STONE   3   //挖石头
#define WORK_GOLD    5   //挖金矿
#define WORK_FARM    6   //种田
#define WORK_BUILD   7   //盖房子,由专职村民负责

//第一波攻击约在6000帧到来,5600帧后停止祭司探路,回安全点交给保护代码,留400帧回程余量
#define PRIEST_EXPLORE_END_FRAME 5600

//探路遇敌后撤帧数,25fps下75帧等于3秒,足够拉开距离再切换目标
#define PRIEST_RETREAT_FRAMES 75

/* ---------- 全局变量,跨帧保存状态 ---------- */
static int gameStage = 1;                 //当前阶段,1探路采集,2防御第一波并升级铜器,3防御第二三波造兵,4反攻
static int townX = -1;                    //市镇中心的块坐标X
static int townY = -1;                    //市镇中心的块坐标Y

static map<int,int> villagerWork;         //村民编号到工种的映射
static map<int,int> lastOrderFrame;       //单位编号到上次下指令帧号的映射

//科技研究标记,研究过就不再研究
static bool hasArrowTech = false;         //谷仓箭塔科技
static bool hasTowerUpTech = false;       //谷仓箭塔升级科技,加攻击和射程
static bool hasWoodTech = false;          //市场的伐木科技
static bool hasWheelTech = false;         //市场的车轮科技
static bool hasGoldTech = false;          //市场的金矿科技
static bool hasFarmTech = false;          //市场的驯养动物科技
static bool hasToolTech = false;          //仓库的工具使用科技
static bool hasInfTech = false;           //仓库的步兵护甲科技
static bool hasAxeTech = false;           //兵营的战斧升级
static bool hasBroadTech = false;         //兵营的阔剑科技
static bool hasCompositeTech = false;     //靶场的复合弓科技
static bool hasAgeUp = false;             //是否已下令升级时代

static int priestLastBlood = -1;          //祭司上一帧血量,用于受击检测

//祭司探路:固定访问四个点位
static int priestVisitIndex = -1;         //当前访问第几个目标,-1未开始,0~3循环直到探路结束
static int priestVisitOrder[4];           //访问顺序,存点位索引,0~3角点,4为中心
static bool priestVisitSorted = false;    //四点是否已完成到基地的距离排序
static int priestStillFrames = 0;         //祭司连续静止帧数,连续10帧触发到达/重试判定
static int priestStuckTotal = 0;          //祭司真正连续静止的累计帧数(重试不清零),超过50强制推进下一目标
static double priestLastDR = -1.0;        //祭司上一帧细节坐标X,到达判定用
static double priestLastUR = -1.0;        //祭司上一帧细节坐标Y
static bool priestMoveSent = false;       //当前目标的移动指令是否已发送
static int priestRetreatFrames = 0;       //遇敌后撤剩余帧数,后撤期间不判定到达
static int gameStartFrame = -1;           //本局开始帧号,计算相对帧号用,游戏内重启不重置

//选址基准点,开局抓取一次,新一局重新抓取
//军事建筑以开局箭塔为基准,其余建筑以开局房屋为基准
static bool basePointSet = false;         //是否已抓取开局基准建筑
static int homeBaseX = -1;                //开局自带房屋的块坐标X,其余建筑选址基准
static int homeBaseY = -1;                //开局自带房屋的块坐标Y
static int towerBaseX = -1;               //开局自带箭塔的块坐标X,军事建筑选址基准
static int towerBaseY = -1;               //开局自带箭塔的块坐标Y

//地图信息,0草地,1海洋,-1未探索
static int gameMap[100][100];

/* =====================================================================
 *  辅助函数
 * ===================================================================== */

//一格的长度,块坐标转细节坐标要乘这个数
double UsrAI::blockLength()
{
    return (double)BLOCKSIDELENGTH;
}

//块坐标转细节坐标,取格子中心
double UsrAI::detailOf(int block)
{
    double d = (block + 0.5) * blockLength();
    return d;
}

//两个块坐标的直线距离
double UsrAI::distanceBlock(int x1, int y1, int x2, int y2)
{
    int dx = x1 - x2;
    int dy = y1 - y2;
    double d = sqrt((double)(dx * dx + dy * dy));
    return d;
}

//判断单位能否下指令,防止同一帧重复下;gap是最少间隔帧数
bool UsrAI::canOrder(int sn, int gap)
{
    map<int,int>::iterator it = lastOrderFrame.find(sn);
    if (it == lastOrderFrame.end()) {
        return true;      //从未下过指令
    }
    int lastFrame = it->second;
    if (g_frame - lastFrame >= gap) {
        return true;      //间隔已足够
    }
    return false;         //间隔不足,再等等
}

//记录单位下指令的帧号
void UsrAI::rememberOrder(int sn)
{
    lastOrderFrame[sn] = g_frame;
}

//统计某类建筑数量,含未建完的
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

//找第一个建好的某类建筑编号,没有返回-1
int UsrAI::findBuilding(const tagInfo& info, int buildType)
{
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].Type == buildType && info.buildings[i].Percent >= 100) {
            return info.buildings[i].SN;
        }
    }
    return -1;
}

//判断某建筑是否正在忙(Project不为0)
bool UsrAI::isBuildingBusy(const tagInfo& info, int sn)
{
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].SN == sn && info.buildings[i].Project != 0) {
            return true;
        }
    }
    return false;
}

//找空闲村民,优先之前的建造工,其次任意空闲村民;找不到返回-1
int UsrAI::findIdleWorker(const tagInfo& info)
{
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
            return farmer.SN;
        }
    }
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        const tagFarmer& farmer = info.farmers[i];
        if (farmer.FarmerSort != 0) {
            continue;
        }
        if (farmer.NowState == HUMAN_STATE_IDLE) {
            return farmer.SN;
        }
    }
    return -1;
}

//统计某类士兵数量
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
 *  找某类资源的存放点
 *  浆果放谷仓,木材、石头、金矿放仓库,都没有就放市镇中心,市镇中心能存所有资源。
 * ===================================================================== */
bool UsrAI::findDropoff(const tagInfo& info, int resType, int& bx, int& by)
{
    //浆果和农田食物放谷仓,其余放仓库
    int buildType = BUILDING_STOCK;
    if (resType == RESOURCE_BUSH) {
        buildType = BUILDING_GRANARY;
    }
    int dropSN = findBuilding(info, buildType);
    if (dropSN < 0) {
        dropSN = findBuilding(info, BUILDING_CENTER);    //市镇中心能存所有资源
    }
    if (dropSN < 0) {
        return false;    //市镇中心都没有,无存放点
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
 *  找离存放点最近的某类资源
 *  以存放点为基准选最近的一个,村民交资源的返程最短,采集周期耗时最少。
 *  exclude里是本帧已分给其他村民的资源SN,避免多人抢同一块。
 * ===================================================================== */
int UsrAI::findResourceNearDropoff(const tagInfo& info, int resType, const std::set<int>& exclude)
{
    //找存放点,浆果归谷仓,其他归仓库,都没有归市镇中心
    int dropX = 0, dropY = 0;
    if (!findDropoff(info, resType, dropX, dropY)) {
        return -1;
    }
    //遍历该类资源,选离存放点最近且本帧未被占用的
    int bestSN = -1;
    double bestDistance = 99999999.0;
    for (unsigned int i = 0; i < info.resources.size(); i++) {
        if (info.resources[i].Type != resType) {
            continue;    //类型不符
        }
        if (info.resources[i].Cnt <= 0) {
            continue;    //资源已采完
        }
        if (exclude.count(info.resources[i].SN) > 0) {
            continue;    //本帧已被别的村民选走
        }
        double d = distanceBlock(dropX, dropY, info.resources[i].BlockDR, info.resources[i].BlockUR);
        if (d < bestDistance) {
            bestDistance = d;
            bestSN = info.resources[i].SN;
        }
    }
    return bestSN;
}

//返回资源的块占地边长,对齐内核尺寸:石头/金矿2x2,树木含2x2树林(AI侧同为RESOURCE_TREE)保守按2,其余1x1
int UsrAI::resourceBlockSize(int resType)
{
    if (resType == RESOURCE_STONE || resType == RESOURCE_GOLD || resType == RESOURCE_TREE) {
        return 2;
    }
    return 1;
}

//判断(x,y)能否放下size*size的建筑
bool UsrAI::canBuildHere(const tagInfo& info, int x, int y, int size)
{
    //检查是否超出地图
    if (x < 0 || y < 0 || x + size > 100 || y + size > 100) {
        return false;
    }
    //检查区域内是否全是草地且高度一致
    int firstHeight = -999;
    for (int i = x; i < x + size; i++) {
        for (int j = y; j < y + size; j++) {
            if (info.theMap == NULL) {
                return false;
            }
            const tagTerrain& t = (*info.theMap)[i][j];
            if (t.type == MAPPATTERN_UNKNOWN) {
                return false;    //未探索
            }
            if (t.type != MAPPATTERN_GRASS) {
                return false;    //非草地
            }
            if (firstHeight == -999) {
                firstHeight = t.height;
            } else {
                if (t.height != firstHeight) {
                    return false;    //高度不一致
                }
            }
        }
    }
    //检查是否与已有建筑重叠,建筑间至少留1格通道,否则单位会被卡住
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        int bX = info.buildings[i].BlockDR;
        int bY = info.buildings[i].BlockUR;
        int bSize = 3;
        if (info.buildings[i].Type == BUILDING_HOME || info.buildings[i].Type == BUILDING_ARROWTOWER) {
            bSize = 2;    //房子和箭塔是2x2
        }
        //对方建筑范围外扩1格,保证留出通道,再判重叠
        int bx1 = bX - 1;
        int by1 = bY - 1;
        int bx2 = bX + bSize + 1;
        int by2 = bY + bSize + 1;
        if (bx1 < x + size && bx2 > x && by1 < y + size && by2 > y) {
            return false;
        }
    }
    //检查是否与敌方建筑重叠
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
    //检查是否与资源重叠,树、石头、金矿等;石头/金矿/树林为2x2,按占地矩形判相交
    for (unsigned int i = 0; i < info.resources.size(); i++) {
        int rX = info.resources[i].BlockDR;
        int rY = info.resources[i].BlockUR;
        int rSize = resourceBlockSize(info.resources[i].Type);
        if (rX < x + size && rX + rSize > x && rY < y + size && rY + rSize > y) {
            return false;
        }
    }
    //检查是否与村民重叠
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        int fX = info.farmers[i].BlockDR;
        int fY = info.farmers[i].BlockUR;
        if (fX >= x && fX < x + size && fY >= y && fY < y + size) {
            return false;
        }
    }
    //检查是否与己方士兵重叠
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        int aX = info.armies[i].BlockDR;
        int aY = info.armies[i].BlockUR;
        if (aX >= x && aX < x + size && aY >= y && aY < y + size) {
            return false;
        }
    }
    //检查是否与敌方士兵重叠
    for (unsigned int i = 0; i < info.enemy_armies.size(); i++) {
        int aX = info.enemy_armies[i].BlockDR;
        int aY = info.enemy_armies[i].BlockUR;
        if (aX >= x && aX < x + size && aY >= y && aY < y + size) {
            return false;
        }
    }
    return true;    //无重叠,可建
}

//以(centerX,centerY)为中心由内向外一圈圈找能放下size*size建筑的空地
//找到后坐标写入x和y,返回true
bool UsrAI::findBuildPlace(const tagInfo& info, int& x, int& y, int size, int centerX, int centerY)
{
    //半径4到18逐圈搜索,太近会被建筑围死
    for (int r = 4; r <= 18; r++) {
        //上边
        for (int dx = -r; dx <= r; dx++) {
            if (canBuildHere(info, centerX + dx, centerY - r, size)) {
                x = centerX + dx;
                y = centerY - r;
                return true;
            }
        }
        //下边
        for (int dx = -r; dx <= r; dx++) {
            if (canBuildHere(info, centerX + dx, centerY + r, size)) {
                x = centerX + dx;
                y = centerY + r;
                return true;
            }
        }
        //左边
        for (int dy = -r; dy <= r; dy++) {
            if (canBuildHere(info, centerX - r, centerY + dy, size)) {
                x = centerX - r;
                y = centerY + dy;
                return true;
            }
        }
        //右边
        for (int dy = -r; dy <= r; dy++) {
            if (canBuildHere(info, centerX + r, centerY + dy, size)) {
                x = centerX + r;
                y = centerY + dy;
                return true;
            }
        }
    }
    return false;    //无合适空地
}

/* =====================================================================
 *  获取选址基准点,按建筑类型分区选址
 *  军事建筑兵营、靶场、马厩、学院、箭塔以开局箭塔为基准,其余建筑以开局房屋为基准,
 *  基准缺失时依次回退,最后回退市镇中心。
 * ===================================================================== */
void UsrAI::getBuildBase(int category, int& bx, int& by)
{
    int tx = -1, ty = -1;
    if (category == 1) {
        tx = towerBaseX;    //军事建筑用开局箭塔
        ty = towerBaseY;
        if (tx < 0) {
            tx = homeBaseX;    //无箭塔则用开局房屋
            ty = homeBaseY;
        }
    } else {
        tx = homeBaseX;    //其余建筑用开局房屋
        ty = homeBaseY;
    }
    if (tx < 0) {
        tx = townX;    //无房屋则回退市镇中心
        ty = townY;
    }
    bx = tx;
    by = ty;
}

/* =====================================================================
 *  找祭司的编号和位置
 *  敌波会专门来杀祭司,军队要守在他旁边,所以要经常找他。
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
 *  找祭司的安全点,有箭塔就躲箭塔旁,没有就去市镇中心旁
 *  祭司是胜利关键,藏身处尽量有箭塔罩着。
 * ===================================================================== */
bool UsrAI::findPriestSafeSpot(const tagInfo& info, int& x, int& y)
{
    //有箭塔就躲箭塔旁
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
    //无箭塔就躲市镇中心旁
    if (townX >= 0 && townY >= 0) {
        x = townX + 1;
        y = townY + 1;
        return true;
    }
    return false;
}

/* =====================================================================
 *  在已探明且可到达区域中,找离目标角点最近的格子作为临时目标点
 *  角点可能尚未探明或不可走(海洋),不能直接下令前往;
 *  从祭司当前位置出发,只经过gameMap==0(已探明草地)做泛洪,
 *  泛洪可达的格子即"已探明且可到达",在其中取距角点最近者。
 * ===================================================================== */
bool UsrAI::findTempTargetNearCorner(int cornerX, int cornerY, int priestBX, int priestBY,
                                     int& outX, int& outY)
{
    //可达标记
    bool reach[100][100];
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 100; j++) {
            reach[i][j] = false;
        }
    }

    //BFS队列,最多100*100个格子
    int qx[10000], qy[10000];
    int head = 0, tail = 0;
    //起点必须是已探明草地
    if (priestBX >= 0 && priestBX < 100 && priestBY >= 0 && priestBY < 100
        && gameMap[priestBX][priestBY] == 0) {
        reach[priestBX][priestBY] = true;
        qx[tail] = priestBX;
        qy[tail] = priestBY;
        tail++;
    }
    //四方向泛洪,只走已探明草地
    const int stepX[4] = { 1, -1, 0,  0 };
    const int stepY[4] = { 0,  0, 1, -1 };
    while (head < tail) {
        int cx = qx[head];
        int cy = qy[head];
        head++;
        for (int d = 0; d < 4; d++) {
            int nx = cx + stepX[d];
            int ny = cy + stepY[d];
            if (nx < 0 || nx >= 100 || ny < 0 || ny >= 100) {
                continue;
            }
            if (reach[nx][ny]) {
                continue;
            }
            if (gameMap[nx][ny] != 0) {
                continue;    //海洋或未探明不可走
            }
            reach[nx][ny] = true;
            qx[tail] = nx;
            qy[tail] = ny;
            tail++;
        }
    }

    //在可达集合中找距目标角点最近的格子
    int bestX = priestBX;
    int bestY = priestBY;
    int bestDist = 1 << 30;
    bool found = false;
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 100; j++) {
            if (!reach[i][j]) {
                continue;
            }
            int d = abs(i - cornerX) + abs(j - cornerY);
            if (d < bestDist) {
                bestDist = d;
                bestX = i;
                bestY = j;
                found = true;
            }
        }
    }

    if (!found) {
        //无可达已探明草地,兜底返回祭司当前位置
        outX = priestBX;
        outY = priestBY;
        return false;
    }
    outX = bestX;
    outY = bestY;
    return true;
}

/* =====================================================================
 *  地图信息更新
 * ===================================================================== */
void UsrAI::updateMapInfo(const tagInfo& info)
{
    //theMap为空则保持上帧地图
    if (info.theMap == NULL) {
        return;
    }
    //按theMap填入地形,0草地,1海洋,-1未探索
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 100; j++) {
            const tagTerrain& t = (*info.theMap)[i][j];
            if (t.type == MAPPATTERN_GRASS) {
                gameMap[i][j] = 0;
            } else if (t.type == MAPPATTERN_OCEAN) {
                gameMap[i][j] = 1;
            } else {
                gameMap[i][j] = -1;
            }
        }
    }
}

/* =====================================================================
 *  给空闲村民分配工作
 * ===================================================================== */
void UsrAI::assignWork(const tagInfo& info)
{
    //删除已死亡村民的记录
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

    //统计当前各工种人数
    int numBerry = 0;   //采浆果的人数
    int numWood = 0;    //砍树的人数
    int numStone = 0;   //挖石头的人数
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
        } else if (work == WORK_GOLD) {
            numGold++;
        } else if (work == WORK_FARM) {
            numFarm++;
        }
        it2++;
    }

    //统计村民总数
    int villagerNum = 0;
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        if (info.farmers[i].FarmerSort == 0) {
            villagerNum++;
        }
    }

    //砍树人数多留几个,盖房、兵营、箭塔、农田都要木头
    int wantWood = 5;

    //收集已占用的农田,即正在种田村民的工作目标,让新分配的农民避开
    //一块农田只能一人采集,多人采不增加效率
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
    //本帧已分出的农田,避免同帧多个农民选同一块
    set<int> farmChosenThisFrame;

    //本帧已分出的资源,避免同帧多个农民抢同一处
    set<int> resourceChosenThisFrame;

    //挨个看每个村民,如果他是空闲的,就给他安排工作
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        const tagFarmer& farmer = info.farmers[i];
        //只处理陆地村民,渔船运输船不管
        if (farmer.FarmerSort != 0) {
            continue;
        }
        //忙碌则跳过
        int state = farmer.NowState;
        bool isIdle = false;
        if (state == HUMAN_STATE_IDLE) {
            isIdle = true;
        } else if (state == HUMAN_STATE_WORKING) {
            //工作中但工作对象消失,视为空闲
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
        //盖房村民由专门函数管理,这里跳过
        map<int,int>::iterator workIt = villagerWork.find(farmer.SN);
        int nowWork = WORK_NONE;
        if (workIt != villagerWork.end()) {
            nowWork = workIt->second;
        }
        if (nowWork == WORK_BUILD) {
            continue;
        }
        //限制下指令频率
        if (!canOrder(farmer.SN, 10)) {
            continue;
        }

        //按缺人情况决定工种
        int chooseWork = WORK_WOOD;    //默认砍树
        if (gameStage <= 2) {
            //阶段1和2,食物为主,金矿铜器后才有需求
            //浆果4人,石头1人,农田4块,砍树5人
            if (numBerry < 4) {
                chooseWork = WORK_BERRY;
            } else if (numStone < 1) {
                chooseWork = WORK_STONE;
            } else if (numFarm < 4) {
                chooseWork = WORK_FARM;
            } else if (numWood < wantWood) {
                chooseWork = WORK_WOOD;
            } else {
                chooseWork = WORK_WOOD;    //保底砍树,不闲置
            }
        } else {
            //铜器时代,金矿3人,石头2人,农田6块,砍树补足
            if (numStone < 2) {
                chooseWork = WORK_STONE;
            } else if (numGold < 3) {
                chooseWork = WORK_GOLD;
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
            //选离存放点最近的浆果,返程交粮路程最短
            targetSN = findResourceNearDropoff(info, RESOURCE_BUSH, resourceChosenThisFrame);
        } else if (chooseWork == WORK_STONE) {
            //选离存放点最近的石头
            targetSN = findResourceNearDropoff(info, RESOURCE_STONE, resourceChosenThisFrame);
        } else if (chooseWork == WORK_GOLD) {
            //选离存放点最近的金矿
            targetSN = findResourceNearDropoff(info, RESOURCE_GOLD, resourceChosenThisFrame);
        } else if (chooseWork == WORK_FARM) {
            //种田,每人一块,避开已占用的田,选离存放点最近的
            int dropX = 0, dropY = 0;
            if (!findDropoff(info, RESOURCE_BUSH, dropX, dropY)) {
                dropX = farmer.BlockDR;    //无存放点则按村民位置选
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
                    continue;    //已有人种
                }
                double d = distanceBlock(dropX, dropY, b.BlockDR, b.BlockUR);
                if (d < bestFarmDist) {
                    bestFarmDist = d;
                    bestFarmSN = b.SN;
                }
            }
            if (bestFarmSN >= 0) {
                targetSN = bestFarmSN;
                farmChosenThisFrame.insert(bestFarmSN);    //本帧不再分给他人
            }
        } else {
            //选离存放点最近的树
            targetSN = findResourceNearDropoff(info, RESOURCE_TREE, resourceChosenThisFrame);
        }

        //无目标资源则改砍树
        if (targetSN < 0) {
            if (chooseWork != WORK_WOOD) {
                chooseWork = WORK_WOOD;
                targetSN = findResourceNearDropoff(info, RESOURCE_TREE, resourceChosenThisFrame);
            }
        }
        if (targetSN < 0) {
            continue;    //树也没有,跳过
        }

        //下令干活
        HumanAction(farmer.SN, targetSN);
        rememberOrder(farmer.SN);
        villagerWork[farmer.SN] = chooseWork;
        resourceChosenThisFrame.insert(targetSN);    //本帧不再分给他人
    }
}

/* =====================================================================
 *  派一个村民盖房子
 * ===================================================================== */
void UsrAI::buildHouse(const tagInfo& info)
{
    //每间房子加4人口上限,前期盖6间,铜器后盖12间
    int houseNum = countBuilding(info, BUILDING_HOME);
    int wantHouse = 6;
    if (gameStage >= 3) {
        wantHouse = 12;    //铜器后12间
    }
    if (houseNum >= wantHouse) {
        return;    //数量已够
    }
    //木材不足则等待
    if (info.Wood < BUILD_HOUSE_WOOD) {
        return;
    }
    //找空闲村民,优先之前的建造工
    int workerSN = findIdleWorker(info);
    if (workerSN < 0) {
        return;    //无空闲村民
    }
    if (!canOrder(workerSN, 15)) {
        return;
    }
    //房屋以开局房屋为基准选址,形成居民区
    int bx = 0;
    int by = 0;
    int baseX = 0, baseY = 0;
    getBuildBase(0, baseX, baseY);
    if (!findBuildPlace(info, bx, by, 2, baseX, baseY)) {
        return;
    }
    //下令盖房
    HumanBuild(workerSN, BUILDING_HOME, bx, by);
    rememberOrder(workerSN);
    villagerWork[workerSN] = WORK_BUILD;
}

/* =====================================================================
 *  派一个村民盖其他建筑
 * ===================================================================== */
void UsrAI::buildSomeBuilding(const tagInfo& info, int buildingType)
{
    //已建成则不再建
    int builtNum = 0;
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].Type == buildingType && info.buildings[i].Percent >= 100) {
            builtNum++;
        }
    }
    if (builtNum >= 1) {
        return;
    }
    //检查木材是否足够
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
    //有在建的同类型建筑则等待
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].Type == buildingType && info.buildings[i].Percent < 100) {
            return;
        }
    }
    //找空闲村民,优先之前的建造工
    int workerSN = findIdleWorker(info);
    if (workerSN < 0) {
        return;
    }
    if (!canOrder(workerSN, 15)) {
        return;
    }
    //建筑尺寸,箭塔2x2,其余3x3
    int size = 3;
    if (buildingType == BUILDING_ARROWTOWER) {
        size = 2;
    }
    //军事建筑以开局箭塔为基准选址,其余建筑以开局房屋为基准
    int bx = 0;
    int by = 0;
    bool isMilitary = (buildingType == BUILDING_ARMYCAMP || buildingType == BUILDING_RANGE
                       || buildingType == BUILDING_STABLE || buildingType == BUILDING_COLLAGE
                       || buildingType == BUILDING_ARROWTOWER);
    int baseX = 0, baseY = 0;
    getBuildBase(isMilitary ? 1 : 0, baseX, baseY);
    if (!findBuildPlace(info, bx, by, size, baseX, baseY)) {
        return;
    }
    //下令建造
    HumanBuild(workerSN, buildingType, bx, by);
    rememberOrder(workerSN);
    villagerWork[workerSN] = WORK_BUILD;
}

/* =====================================================================
 *  派一个村民盖农田
 *  浆果会采完,不种田后期没食物,村民和士兵都造不出来;农田需要先建市场。
 * ===================================================================== */
void UsrAI::buildFarm(const tagInfo& info)
{
    //市场是前置建筑,没有则不盖
    if (findBuilding(info, BUILDING_MARKET) < 0) {
        return;
    }
    //统计有剩余食物的农田,前期4块,铜器后6块
    int liveFarm = 0;
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].Type == BUILDING_FARM && info.buildings[i].Percent >= 100
            && info.buildings[i].Cnt > 0) {
            liveFarm++;
        }
    }
    int wantFarm = 4;
    if (gameStage >= 3) {
        wantFarm = 6;    //铜器后6块
    }
    if (liveFarm >= wantFarm) {
        return;
    }
    //木材不足则等待
    if (info.Wood < BUILD_FARM_WOOD) {
        return;
    }
    //有在建农田则等待,避免多人同时盖
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        if (info.buildings[i].Type == BUILDING_FARM && info.buildings[i].Percent < 100) {
            return;
        }
    }
    //找空闲村民,优先之前的建造工
    int workerSN = findIdleWorker(info);
    if (workerSN < 0) {
        return;    //无空闲村民
    }
    if (!canOrder(workerSN, 15)) {
        return;
    }
    //农田选址,优先市镇中心上下左右,其次四个对角
    //市镇中心3x3,建筑间留1格通道,偏移4格正好紧贴不挡路
    int bx = 0;
    int by = 0;
    const int farmOffX[8] = { 0, 0, -4, 4, -4, -4, 4, 4 };   //上、下、左、右、左上、左下、右上、右下
    const int farmOffY[8] = { -4, 4, 0, 0, -4, 4, -4, 4 };
    bool placed = false;
    for (int i = 0; i < 8; i++) {
        if (canBuildHere(info, townX + farmOffX[i], townY + farmOffY[i], 3)) {
            bx = townX + farmOffX[i];
            by = townY + farmOffY[i];
            placed = true;
            break;
        }
    }
    //8个方位都不行,回退为以市镇中心圈层搜索
    if (!placed) {
        if (!findBuildPlace(info, bx, by, 3, townX, townY)) {
            return;    //无空地
        }
    }
    //下令盖田
    HumanBuild(workerSN, BUILDING_FARM, bx, by);
    rememberOrder(workerSN);
    villagerWork[workerSN] = WORK_BUILD;
}

/* =====================================================================
 *  研究科技
 *  造兵永远优先。第二波之前只研究箭塔科技、箭塔升级和工具使用,
 *  其余科技推后,把食物和建筑时间省下来造兵。
 * ===================================================================== */
void UsrAI::researchTech(const tagInfo& info)
{
    //谷仓先研究箭塔科技,村民才能盖箭塔
    if (!hasArrowTech && info.Meat >= BUILDING_GRANARY_ARROWTOWER_FOOD) {
        int granarySN = findBuilding(info, BUILDING_GRANARY);
        if (granarySN >= 0 && !isBuildingBusy(info, granarySN)) {
            BuildingAction(granarySN, BUILDING_GRANARY_ARROWTOWER);
            hasArrowTech = true;
        }
    }

    //铜器时代谷仓研究箭塔升级,箭塔加1攻击加1射程
    if (gameStage >= 3 && !hasTowerUpTech
        && info.Meat >= BUILDING_GRANARY_UPGRADE_ARROWTOWER_FOOD
        && info.Stone >= BUILDING_GRANARY_UPGRADE_ARROWTOWER_STONE) {
        int granarySN = findBuilding(info, BUILDING_GRANARY);
        if (granarySN >= 0 && !isBuildingBusy(info, granarySN)) {
            BuildingAction(granarySN, BUILDING_GRANARY_ARROWTOWE_UPGRADE);
            hasTowerUpTech = true;
        }
    }

    //仓库研究工具使用,加攻击,便宜,前期就研究
    if (!hasToolTech && info.Meat >= BUILDING_STOCK_UPGRADE_CLOSER_ATTACK_FOOD) {
        int stockSN = findBuilding(info, BUILDING_STOCK);
        if (stockSN >= 0 && !isBuildingBusy(info, stockSN)) {
            BuildingAction(stockSN, BUILDING_STOCK_UPGRADE_USETOOL);
            hasToolTech = true;
        }
    }

    //========== 6000帧第一波后:兵营战斧升级 ==========
    if (info.GameFrame >= 6000 && !hasAxeTech && info.Meat >= BUILDING_ARMYCAMP_UPGRADE_CLUBMAN_FOOD) {
        int campSN = findBuilding(info, BUILDING_ARMYCAMP);
        if (campSN >= 0 && !isBuildingBusy(info, campSN)) {
            BuildingAction(campSN, BUILDING_ARMYCAMP_UPGRADE_CLUBMAN);
            hasAxeTech = true;
        }
    }

    //========== 13500帧第二波后:阔剑、复合弓、步兵护甲 ==========
    if (info.GameFrame >= 13500) {
        if (!hasBroadTech && info.Meat >= BUILDING_ARMYCAMP_UPGRADE_BROADSWORD_FOOD
            && info.Gold >= BUILDING_ARMYCAMP_UPGRADE_BROADSWORD_GOLD) {
            int campSN = findBuilding(info, BUILDING_ARMYCAMP);
            if (campSN >= 0 && !isBuildingBusy(info, campSN)) {
                BuildingAction(campSN, BUILDING_ARMYCAMP_UPGRADE_BROADSWORD);
                hasBroadTech = true;
            }
        }
        if (!hasCompositeTech && info.Meat >= BUILDING_RANGE_UPGRADE_COMPOSITE_BOW_FOOD
            && info.Wood >= BUILDING_RANGE_UPGRADE_COMPOSITE_BOW_WOOD) {
            int rangeSN = findBuilding(info, BUILDING_RANGE);
            if (rangeSN >= 0 && !isBuildingBusy(info, rangeSN)) {
                BuildingAction(rangeSN, BUILDING_RANGE_UPGRADE_COMPOSITE_BOW);
                hasCompositeTech = true;
            }
        }
        if (!hasInfTech && info.Meat >= BUILDING_STOCK_UPGRADE_DEFENSE_INFANTRY_FOOD) {
            int stockSN = findBuilding(info, BUILDING_STOCK);
            if (stockSN >= 0 && !isBuildingBusy(info, stockSN)) {
                BuildingAction(stockSN, BUILDING_STOCK_UPGRADE_DEFENSE_INFANTRY);
                hasInfTech = true;
            }
        }
    }

    //========== 21000帧第三波后:市场伐木、车轮、金矿、驯养动物 ==========
    if (info.GameFrame >= 21000) {
        if (!hasWoodTech && info.Meat >= BUILDING_MARKET_WOOD_UPGRADE_FOOD
            && info.Wood >= BUILDING_MARKET_WOOD_UPGRADE_WOOD) {
            int marketSN = findBuilding(info, BUILDING_MARKET);
            if (marketSN >= 0 && !isBuildingBusy(info, marketSN)) {
                BuildingAction(marketSN, BUILDING_MARKET_WOOD_UPGRADE);
                hasWoodTech = true;
            }
        }
        if (!hasWheelTech && info.Meat >= BUILDING_MARKET_WHEEL_UPGRADE_FOOD
            && info.Wood >= BUILDING_MARKET_WHEEL_UPGRADE_WOOD) {
            int marketSN = findBuilding(info, BUILDING_MARKET);
            if (marketSN >= 0 && !isBuildingBusy(info, marketSN)) {
                BuildingAction(marketSN, BUILDING_MARKET_WHEEL_UPGRADE);
                hasWheelTech = true;
            }
        }
        if (!hasGoldTech && info.Meat >= BUILDING_MARKET_GOLD_UPGRADE_FOOD
            && info.Wood >= BUILDING_MARKET_GOLD_UPGRADE_WOOD) {
            int marketSN = findBuilding(info, BUILDING_MARKET);
            if (marketSN >= 0 && !isBuildingBusy(info, marketSN)) {
                BuildingAction(marketSN, BUILDING_MARKET_GOLD_UPGRADE);
                hasGoldTech = true;
            }
        }
        if (!hasFarmTech && info.Meat >= BUILDING_MARKET_FARM_UPGRADE_FOOD
            && info.Wood >= BUILDING_MARKET_FARM_UPGRADE_WOOD) {
            int marketSN = findBuilding(info, BUILDING_MARKET);
            if (marketSN >= 0 && !isBuildingBusy(info, marketSN)) {
                BuildingAction(marketSN, BUILDING_MARKET_FARM_UPGRADE);
                hasFarmTech = true;
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
    //市镇中心忙则等待
    if (isBuildingBusy(info, centerSN)) {
        return;
    }
    //统计村民数量
    int villagerNum = 0;
    for (unsigned int i = 0; i < info.farmers.size(); i++) {
        if (info.farmers[i].FarmerSort == 0) {
            villagerNum++;
        }
    }
    //村民前期14个,铜器后16个,人口留给造兵,兵太少第二波顶不住
    int wantVillager = 14;
    if (gameStage >= 3) {
        wantVillager = 16;    //铜器后16个
    }
    //食物足、村民不足、人口未满时生产
    if (villagerNum < wantVillager && info.Meat >= BUILDING_CENTER_CREATEFARMER_FOOD) {
        if (info.Human_Num + 1 <= info.Human_MaxNum) {
            BuildingAction(centerSN, BUILDING_CENTER_CREATEFARMER);
        }
    }
}

/* =====================================================================
 *  让市镇中心从工具时代升级到铜器时代
 * ===================================================================== */
void UsrAI::upgradeAge(const tagInfo& info)
{
    //已下令升级或已非工具时代则跳过
    if (hasAgeUp) {
        return;
    }
    if (info.civilizationStage != CIVILIZATION_TOOLAGE) {
        return;
    }
    //升级需800食物
    if (info.Meat < 800) {
        return;
    }
    //需先建好市场、马厩、靶场中的两个,游戏规则要求
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
        return;    //前置建筑不足
    }
    int centerSN = findBuilding(info, BUILDING_CENTER);
    if (centerSN < 0) {
        return;
    }
    //市镇中心忙则等待
    if (isBuildingBusy(info, centerSN)) {
        return;
    }
    //下令升级
    BuildingAction(centerSN, BUILDING_CENTER_UPGRADE);
    hasAgeUp = true;
}

/* =====================================================================
 *  让兵营、靶场、马厩、学院生产士兵
 *  造兵永远比科技优先,兵少守不住。
 *  工具时代造棍棒兵和弓箭手保底,第一波4分钟就到;
 *  铜器时代主力是方阵兵和弓箭手,棍棒兵当炮灰;
 *  目标数跟着波次走,第一波前6个,第二波前18个,第三波前22个。
 * ===================================================================== */
void UsrAI::makeArmy(const tagInfo& info)
{
    //人口满则停止
    if (info.Human_Num + 1 > info.Human_MaxNum) {
        return;
    }
    //第一波前只造3名棍棒兵,不造弓箭手等其他兵种
    if (gameStage == 1) {
        if (countArmy(info, AT_CLUBMAN) >= 3) {
            return;
        }
        int earlyCampSN = findBuilding(info, BUILDING_ARMYCAMP);
        if (earlyCampSN < 0 || isBuildingBusy(info, earlyCampSN)) {
            return;
        }
        if (info.Meat >= BUILDING_ARMYCAMP_CREATE_CLUBMAN_FOOD) {
            BuildingAction(earlyCampSN, BUILDING_ARMYCAMP_CREATE_CLUBMAN);
            rememberOrder(earlyCampSN);
        }
        return;
    }
    //目标兵力数,含祭司和侦察兵
    int wantArmy = 6;                 //第一波前6个
    if (info.GameFrame >= 13500) {
        wantArmy = 18;                //第二波前18个
    }
    if (info.GameFrame >= 21000) {
        wantArmy = 22;                //第三波前22个
    }
    if (gameStage == 4) {
        wantArmy = 26;                //反攻阶段26个
    }
    if ((int)info.armies.size() >= wantArmy) {
        return;
    }

    //========== 学院:方阵兵,铜器主力,血厚攻高顶前排 ==========
    if (gameStage >= 3) {
        int collageSN = findBuilding(info, BUILDING_COLLAGE);
        if (collageSN >= 0 && !isBuildingBusy(info, collageSN)) {
            if (countArmy(info, AT_HOPLITE) < 8) {
                if (info.Meat >= BUILDING_COLLAGE_CREATE_HOPLITE_FOOD
                    && info.Gold >= BUILDING_COLLAGE_CREATE_HOPLITE_GOLD) {
                    BuildingAction(collageSN, BUILDING_COLLAGE_CREATE_HOPLITE);
                    return;
                }
            }
        }
    }

    //========== 靶场:弓箭手远程输出,有复合弓科技则造复合弓兵 ==========
    int rangeSN = findBuilding(info, BUILDING_RANGE);
    if (rangeSN >= 0 && !isBuildingBusy(info, rangeSN)) {
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

    //========== 兵营:棍棒兵便宜当炮灰,有阔剑科技则造阔剑兵 ==========
    int campSN = findBuilding(info, BUILDING_ARMYCAMP);
    if (campSN >= 0 && !isBuildingBusy(info, campSN)) {
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

    //========== 马厩:侦察骑兵探路用,2个就够 ==========
    if (gameStage >= 3) {
        int stableSN = findBuilding(info, BUILDING_STABLE);
        if (stableSN >= 0 && !isBuildingBusy(info, stableSN)) {
            if (countArmy(info, AT_SCOUT) < 2) {
                if (info.Meat >= BUILDING_STABLE_CREATE_SCOUT_FOOD) {
                    BuildingAction(stableSN, BUILDING_STABLE_CREATE_SCOUT);
                    return;
                }
            }
        }
    }
}

/* =====================================================================
 *  防守,军队围着祭司布防
 *  敌波专门来杀祭司,军队守在祭司旁边,敌人要先打穿军队才能碰到祭司。
 *  无敌人时全员在集结点待命,有敌人时全军集火最近的敌人。
 *  反攻阶段暂未实现。
 * ===================================================================== */
void UsrAI::armyFight(const tagInfo& info)
{
    //反攻阶段暂未实现,当前只防守
    if (gameStage == 4) {
        return;
    }

    //找祭司,军队要守在他旁边
    int priestSN = -1;
    int priestX = 0;
    int priestY = 0;
    double priestDR = 0.0;
    double priestUR = 0.0;
    if (!findPriest(info, priestSN, priestX, priestY, priestDR, priestUR)) {
        return;    //无祭司,祭司死亡即失败
    }

    //集结点放在安全点向外偏2格,军队挡在祭司和敌人之间
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

    //找祭司20格内最近的敌人,优先军队、农民,最后建筑
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
        //无敌人,空闲士兵回集结点;第一波前棍棒兵到箭塔旁边待命
        for (unsigned int i = 0; i < info.armies.size(); i++) {
            const tagArmy& army = info.armies[i];
            if (army.Sort == AT_PRIEST || army.Sort == AT_SCOUT) {
                continue;    //祭司和侦察兵除外
            }
            if (army.NowState != HUMAN_STATE_IDLE) {
                continue;
            }
            //第一波前棍棒兵到箭塔旁边,离箭塔超过2格就移动过去
            if (gameStage == 1 && army.Sort == AT_CLUBMAN) {
                if (distanceBlock(army.BlockDR, army.BlockUR, safeX, safeY) > 2.0) {
                    if (canOrder(army.SN, 30)) {
                        HumanMove(army.SN, detailOf(safeX), detailOf(safeY));
                        rememberOrder(army.SN);
                    }
                }
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

    //有敌人则全军集火最近的敌人
    //第一波前:3名棍棒兵分散分配目标,确保视野内每个敌人至少被一个兵攻击
    if (gameStage == 1) {
        //1.收集祭司20格视野内的全部敌人(军队、农民、建筑)
        std::vector<int> targetList;
        std::vector<double> targetDist;
        for (unsigned int i = 0; i < info.enemy_armies.size(); i++) {
            double d = distanceBlock(info.enemy_armies[i].BlockDR, info.enemy_armies[i].BlockUR, priestX, priestY);
            if (d <= 20.0) {
                targetList.push_back(info.enemy_armies[i].SN);
                targetDist.push_back(d);
            }
        }
        for (unsigned int i = 0; i < info.enemy_farmers.size(); i++) {
            double d = distanceBlock(info.enemy_farmers[i].BlockDR, info.enemy_farmers[i].BlockUR, priestX, priestY);
            if (d <= 20.0) {
                targetList.push_back(info.enemy_farmers[i].SN);
                targetDist.push_back(d);
            }
        }
        for (unsigned int i = 0; i < info.enemy_buildings.size(); i++) {
            double d = distanceBlock(info.enemy_buildings[i].BlockDR, info.enemy_buildings[i].BlockUR, priestX, priestY);
            if (d <= 20.0) {
                targetList.push_back(info.enemy_buildings[i].SN);
                targetDist.push_back(d);
            }
        }
        //2.敌人按距离从近到远排序,敌人多于兵时优先覆盖最近的
        for (unsigned int a = 0; a < targetList.size(); a++) {
            unsigned int best = a;
            for (unsigned int b = a + 1; b < targetList.size(); b++) {
                if (targetDist[b] < targetDist[best]) {
                    best = b;
                }
            }
            if (best != a) {
                int tmpSN = targetList[a];
                targetList[a] = targetList[best];
                targetList[best] = tmpSN;
                double tmpD = targetDist[a];
                targetDist[a] = targetDist[best];
                targetDist[best] = tmpD;
            }
        }
        //3.收集可参战的棍棒兵(跟随祭司,均在战场范围内)
        std::vector<int> clubList;
        for (unsigned int i = 0; i < info.armies.size(); i++) {
            const tagArmy& army = info.armies[i];
            if (army.Sort != AT_CLUBMAN) {
                continue;
            }
            if (army.NowState != HUMAN_STATE_IDLE && army.NowState != HUMAN_STATE_WALKING) {
                continue;
            }
            clubList.push_back(army.SN);
        }
        //4.目标分配:兵k先一一对应敌人k,保证每个敌人至少一个兵;兵多于敌人则循环补到敌人
        if (!targetList.empty() && !clubList.empty()) {
            for (unsigned int k = 0; k < clubList.size(); k++) {
                if (!canOrder(clubList[k], 8)) {
                    continue;
                }
                unsigned int targetIdx;
                if (k < targetList.size()) {
                    targetIdx = k;    //第一轮:兵k攻击敌人k,分散覆盖
                } else {
                    targetIdx = (k - (unsigned int)targetList.size()) % targetList.size();    //多余兵循环补
                }
                HumanAction(clubList[k], targetList[targetIdx]);
                rememberOrder(clubList[k]);
            }
        }
        return;
    }

    for (unsigned int i = 0; i < info.armies.size(); i++) {
        const tagArmy& army = info.armies[i];
        if (army.Sort == AT_PRIEST) {
            continue;    //祭司负责治疗和转化
        }
        if (army.NowState != HUMAN_STATE_IDLE && army.NowState != HUMAN_STATE_WALKING) {
            continue;    //作战中不重复下令
        }
        //离战场太远的兵不参与,避免来回跑
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
 *  箭塔不会自己攻击,需AI用HumanAction下令,每次只看射程内的敌人。
 * ===================================================================== */
void UsrAI::towerFight(const tagInfo& info)
{
    //箭塔射程,单位格
    const double towerRange = double(DIS_ARROWTOWER);

    //逐座箭塔处理
    for (unsigned int i = 0; i < info.buildings.size(); i++) {
        const tagBuilding& building = info.buildings[i];
        //只处理已建成的
        if (building.Type != BUILDING_ARROWTOWER) {
            continue;
        }
        if (building.Percent < 100) {
            continue;
        }
        //Project为当前攻击目标编号,非-1表示已在攻击
        if (building.Project >= 0) {
            continue;
        }
        //限制指令频率
        if (!canOrder(building.SN, 10)) {
            continue;
        }

        //箭塔中心,2x2建筑中心在BlockDR+1,BlockUR+1
        double towerDR = (building.BlockDR + 1) * blockLength();
        double towerUR = (building.BlockUR + 1) * blockLength();

        //找射程内最近的敌人,优先军队、农民,最后建筑
        const double rangeLimit = towerRange * blockLength();
        int targetSN = -1;
        double bestDistance = 99999999.0;

        //敌方军队,有细节坐标,距离准确
        for (unsigned int j = 0; j < info.enemy_armies.size(); j++) {
            double d = fabs(towerDR - info.enemy_armies[j].DR) + fabs(towerUR - info.enemy_armies[j].UR);
            if (d <= rangeLimit && d < bestDistance) {
                bestDistance = d;
                targetSN = info.enemy_armies[j].SN;
            }
        }
        //敌方农民
        for (unsigned int j = 0; j < info.enemy_farmers.size(); j++) {
            double d = fabs(towerDR - info.enemy_farmers[j].DR) + fabs(towerUR - info.enemy_farmers[j].UR);
            if (d <= rangeLimit && d < bestDistance) {
                bestDistance = d;
                targetSN = info.enemy_farmers[j].SN;
            }
        }
        //军队农民都不在射程,改打建筑,建筑只有块坐标,按中心估算
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
        //射程内无敌人则跳过,敌人进射程会自动再选
        if (targetSN < 0) {
            continue;
        }

        //下令攻击
        HumanAction(building.SN, targetSN);
        rememberOrder(building.SN);
    }
}

/* =====================================================================
 *  祭司开局探路
 *  祭司的寻找由findPriest负责,何时接管由strategyMain负责。
 *  让祭司依次访问探路点位,四角A(12,12)、B(12,88)、C(88,12)、D(88,88)按到市镇中心距离排序,
 *  访问顺序为第二近、第三近、最近、中心点(50,50),循环直到探路结束。
 *  到达判定是细节坐标连续10帧不变,每个目标点只发一次移动指令。
 *  视野16格内发现敌人就用细节坐标精确检测,向安全点后撤3秒,
 *  后撤结束敌人仍在则继续后撤,敌人离开才切换目标。
 * ===================================================================== */
void UsrAI::priestExplore(const tagInfo& info)
{
    //找祭司
    int priestSN = -1;
    int priestX = 0;
    int priestY = 0;
    double priestDR = 0.0;
    double priestUR = 0.0;
    if (!findPriest(info, priestSN, priestX, priestY, priestDR, priestUR)) {
        return;
    }

    //探路目标点,索引0~3为四角,索引4为中心
    const int px[5] = {12, 12, 88, 88, 50};
    const int py[5] = {12, 88, 12, 88, 50};

    //四角按到市镇中心距离排序,只做一次
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
        //访问顺序,第二近、第三近、最近、中心点
        priestVisitOrder[0] = idx[1];   //第二近
        priestVisitOrder[1] = idx[2];   //第三近
        priestVisitOrder[2] = idx[0];   //最近
        priestVisitOrder[3] = 4;        //中心点
        priestVisitSorted = true;
        priestVisitIndex = 0;
        priestMoveSent = false;
        priestStillFrames = 0;
        priestLastDR = priestDR;
        priestLastUR = priestUR;
    }

    //后撤计时,结束前不做到达判定
    if (priestRetreatFrames > 0) {
        priestRetreatFrames--;
        if (priestRetreatFrames <= 0) {
            //后撤结束,检查敌人是否仍在16格内
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
                //敌人仍在,继续后撤,不切换目标
                priestRetreatFrames = PRIEST_RETREAT_FRAMES;
            } else {
                //敌人离开,计算与当前目标点的距离
                int retreatCurIdx = priestVisitOrder[priestVisitIndex];
                double retreatTargetDist = distanceBlock(priestX, priestY, px[retreatCurIdx], py[retreatCurIdx]);
                if (retreatTargetDist < 40.0) {
                    //敌人离开且距当前目标点小于40格,切换下一目标
                    priestVisitIndex = (priestVisitIndex + 1) % 4;
                    priestMoveSent = false;
                    priestStillFrames = 0;
                } else {
                    //后撤后离当前目标点过远(≥40格),不切换,重新向当前目标点移动
                    priestMoveSent = false;
                    priestStillFrames = 0;
                }
            }
        }
        return;    //后撤期间不做到达判定,不发移动指令
    }

    //16格内发现敌人则向安全点后撤3秒,用细节坐标算距离精度更高
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
        //建筑无细节坐标,用detailOf转换
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

    //到达判定,连续10帧位置不变,细节坐标精度更高
    double dx = priestDR - priestLastDR;
    double dy = priestUR - priestLastUR;
    if (dx * dx + dy * dy < 1.0) {   //每帧移动小于1个细节单位视为不动,祭司每帧约走2个单位
        priestStillFrames++;
        priestStuckTotal++;          //连续静止累计,只有真正发生位移才清零
    } else {
        priestStillFrames = 0;
        priestStuckTotal = 0;        //实际移动了,重置卡死累计
        priestLastDR = priestDR;
        priestLastUR = priestUR;
    }

    //推进下一目标有两种情况:
    //1)连续静止10帧且距当前目标点小于15格,视为正常到达;
    //2)连续静止累计超过50帧,说明长时间被卡住,放弃当前目标强制推进;
    //其余情况(静止10帧但距目标≥15且累计未超50)不切换,重新向当前目标点下令
    if (priestStillFrames >= 10 && priestMoveSent) {
        int arriveIdx = priestVisitOrder[priestVisitIndex];
        double arriveTargetDist = distanceBlock(priestX, priestY, px[arriveIdx], py[arriveIdx]);
        bool normalArrive = (arriveTargetDist < 15.0);       //正常到达
        bool stuckForceAdvance = (priestStuckTotal > 50);    //静止累计超50强制推进
        if (normalArrive || stuckForceAdvance) {
            priestVisitIndex = (priestVisitIndex + 1) % 4;   //循环切换
            priestStuckTotal = 0;                            //推进后重置卡死累计
        }
        //无论切换到新目标还是重试当前目标,都复位指令状态,由末尾发令代码重新HumanMove
        priestStillFrames = 0;
        priestMoveSent = false;
    }

    //发送当前目标移动指令,每点只发一次
    if (!priestMoveSent) {
        int tIdx = priestVisitOrder[priestVisitIndex];   //重取最新目标,到达可能已推进index
        //不直接下令去角点(角点可能未探明或不可走),改为在已探明且可到达区域中,
        //选离当前目标角点最近的格子作为临时目标点;找不到时兜底用角点本身
        int tempX = px[tIdx];
        int tempY = py[tIdx];
        findTempTargetNearCorner(px[tIdx], py[tIdx], priestX, priestY, tempX, tempY);
        HumanMove(priestSN, detailOf(tempX), detailOf(tempY));
        priestMoveSent = true;
        priestStillFrames = 0;
        priestLastDR = priestDR;
        priestLastUR = priestUR;
    }
}

/* =====================================================================
 *  祭司保护
 *  祭司是胜利关键,死了就输,所以要一直躲着。
 *  有箭塔就躲箭塔旁,没有就去市镇中心旁;
 *  敌人进14格就跑,弓箭手射程5格、复合弓和战车弓7格、投石车10格,不能被远程白打;
 *  无敌人就回安全点,安全时顺手给身边受伤部队加血。
 * ===================================================================== */
void UsrAI::priestBehavior(const tagInfo& info)
{
    //反攻阶段暂未实现,当前只保祭司和治疗
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
        priestLastBlood = -1;    //祭司死亡,重置血量记录
        return;    //无祭司,祭司死亡即失败
    }

    //取祭司当前血量,findPriest不返回血量,受击检测用
    int priestBlood = 0;
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        if (info.armies[i].SN == priestSN) {
            priestBlood = info.armies[i].Blood;
            break;
        }
    }

    //受击检测,血量比上一帧少说明正在被打,记录后立刻跑向安全点
    bool underAttack = false;
    if (priestLastBlood >= 0 && priestBlood < priestLastBlood) {
        underAttack = true;
    }
    priestLastBlood = priestBlood;    //更新血量

    //找安全点,箭塔旁或市镇中心旁
    int safeX = 0;
    int safeY = 0;
    if (!findPriestSafeSpot(info, safeX, safeY)) {
        return;
    }

    //受击最紧急,立刻跑向安全点,3帧可改一次方向
    if (underAttack) {
        if (canOrder(priestSN, 3)) {
            HumanMove(priestSN, detailOf(safeX), detailOf(safeY));
            rememberOrder(priestSN);
        }
        return;
    }

    //找离祭司最近的敌人,优先军队再农民
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

    //分级响应
    //16格内预警,先往安全点靠,提前动起来
    //12格内危险,紧急逃跑,方向是远离威胁中心加朝向安全点的合成
    //这样既拉开距离又不跑散,始终往有保护的地方靠
    const double warnDist = 16.0;
    const double dangerDist = 12.0;
    if (enemySN >= 0 && enemyDist <= warnDist) {
        if (enemyDist > dangerDist) {
            //12到16格预警,往安全点靠,10帧改一次方向
            if (canOrder(priestSN, 10)) {
                HumanMove(priestSN, detailOf(safeX), detailOf(safeY));
                rememberOrder(priestSN);
            }
            return;
        }
        //12格内危险,紧急逃跑
        //威胁中心取14格内敌人的加权平均位置,越近权重越大,避免只看最近一个被包抄
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

        //远离威胁中心的方向,归一化
        double awayX = priestDR - threatDR;
        double awayY = priestUR - threatUR;
        double awayLen = sqrt(awayX * awayX + awayY * awayY);
        if (awayLen > 0.001) {
            awayX /= awayLen;
            awayY /= awayLen;
        }

        //朝向安全点的方向,归一化
        double safeDR = detailOf(safeX);
        double safeUR = detailOf(safeY);
        double toSafeX = safeDR - priestDR;
        double toSafeY = safeUR - priestUR;
        double toSafeLen = sqrt(toSafeX * toSafeX + toSafeY * toSafeY);
        if (toSafeLen > 0.001) {
            toSafeX /= toSafeLen;
            toSafeY /= toSafeLen;
        }

        //合成逃跑方向,远离敌人占6成,朝安全点占4成
        double runDirX = awayX * 0.6 + toSafeX * 0.4;
        double runDirY = awayY * 0.6 + toSafeY * 0.4;
        double runDirLen = sqrt(runDirX * runDirX + runDirY * runDirY);
        if (runDirLen > 0.001) {
            runDirX /= runDirLen;
            runDirY /= runDirLen;
        }

        //逃跑距离,敌人越近跑越远,6到10格
        double runDist = 6.0 + (dangerDist - enemyDist) / dangerDist * 4.0;
        double runDR = priestDR + runDirX * runDist * blockLength();
        double runUR = priestUR + runDirY * runDist * blockLength();

        //限制在地图内
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

    //无威胁且离安全点远则回去待着
    if (distanceBlock(priestX, priestY, safeX, safeY) > 3.0) {
        if (canOrder(priestSN, 30)) {
            //目标点偏移一格,避免站在建筑正中
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

    //在安全点附近则给身边受伤部队加血
    for (unsigned int i = 0; i < info.armies.size(); i++) {
        const tagArmy& army = info.armies[i];
        if (army.SN == priestSN) {
            continue;    //不能治疗自己
        }
        if (army.Blood >= army.MaxBlood) {
            continue;    //血量已满
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
 *  每帧主逻辑
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
        return;    //无市镇中心则跳过
    }
    townX = newTownX;
    townY = newTownY;

    //新一局检测,农民数量突然减少超过2个说明开了新局
    //游戏内重启不重置GameFrame,但单位列表重新初始化,数量跳回初始值;
    //探路阶段在第一波前,农民不会被大量杀死,误判概率很低
    static int lastFarmerCount = -1;
    int currentFarmerCount = (int)info.farmers.size();
    if (lastFarmerCount >= 0 && currentFarmerCount < lastFarmerCount - 2) {
        gameStartFrame = info.GameFrame;
        gameStage = 1;
        //重置祭司探路状态,避免上一局进度残留
        priestVisitIndex = -1;
        priestVisitSorted = false;
        priestMoveSent = false;
        priestStillFrames = 0;
        priestStuckTotal = 0;
        priestLastDR = -1.0;
        priestLastUR = -1.0;
        priestRetreatFrames = 0;
        //重置选址基准,新地图需重新抓取开局房屋和箭塔坐标
        basePointSet = false;
        homeBaseX = -1;
        homeBaseY = -1;
        towerBaseX = -1;
        towerBaseY = -1;
    }
    lastFarmerCount = currentFarmerCount;

    //开局或新一局重置后,记录开局房屋和箭塔坐标作为选址基准
    if (!basePointSet) {
        for (unsigned int i = 0; i < info.buildings.size(); i++) {
            if (homeBaseX < 0 && info.buildings[i].Type == BUILDING_HOME
                && info.buildings[i].Percent >= 100) {
                homeBaseX = info.buildings[i].BlockDR;
                homeBaseY = info.buildings[i].BlockUR;
            }
            if (towerBaseX < 0 && info.buildings[i].Type == BUILDING_ARROWTOWER
                && info.buildings[i].Percent >= 100) {
                towerBaseX = info.buildings[i].BlockDR;
                towerBaseY = info.buildings[i].BlockUR;
            }
        }
        basePointSet = true;
    }
    if (gameStartFrame < 0) {
        gameStartFrame = info.GameFrame;
    }
    int relativeFrame = info.GameFrame - gameStartFrame;

    //更新地图信息
    updateMapInfo(info);

    //祭司行为接管
    //6000帧第一波到来前由priestExplore探路,5600帧起改由priestBehavior保护回安全点
    if (relativeFrame < PRIEST_EXPLORE_END_FRAME) {
        priestExplore(info);          //祭司开局探路
    } else {
        priestBehavior(info);         //保护祭司,回安全点
    }


    //按攻略分4个阶段
    //阶段1游戏开始,探路和采集资源,第一波之前
    //阶段2防御第一波并升级铜器,到铜器升级完成
    //阶段3防御第二三波,规模化造兵
    //阶段4进攻敌人基地,完成胜利目标
    if (gameStage == 1 && relativeFrame >= 6000) {
        gameStage = 2;    //第一波约4分钟时到来,进入防御阶段
    }
    if (gameStage == 2 && info.civilizationStage == CIVILIZATION_BRONZEAGE) {
        gameStage = 3;    //铜器时代,进入造兵阶段
    }

    //按顺序执行,造兵优先于科技
    makeVillager(info);      //生产村民
    upgradeAge(info);        //升级时代
    makeArmy(info);          //造兵,科技靠后,波次来了才有兵
    researchTech(info);      //研究科技
    buildHouse(info);        //盖房子
    buildSomeBuilding(info, BUILDING_MARKET);      //盖市场
    buildSomeBuilding(info, BUILDING_ARMYCAMP);    //盖兵营
    buildSomeBuilding(info, BUILDING_RANGE);       //盖靶场
    buildSomeBuilding(info, BUILDING_STABLE);      //盖马厩
    buildFarm(info);         //盖农田,保证后期食物
    if (hasArrowTech) {
        buildSomeBuilding(info, BUILDING_ARROWTOWER);   //解锁后盖箭塔
    }
    if (gameStage >= 3) {
        buildSomeBuilding(info, BUILDING_COLLAGE);      //铜器时代盖学院
    }
    assignWork(info);        //给村民安排工作
    armyFight(info);         //军队作战
    towerFight(info);        //箭塔攻击射程内敌人

    //建筑盖完后,把专职建造工解放出来
    bool needBuild = false;
    if (countBuilding(info, BUILDING_HOME) < 6) {
        needBuild = true;    //前期至少6间
    }
    if (gameStage >= 3 && countBuilding(info, BUILDING_HOME) < 12) {
        needBuild = true;    //铜器后至少12间
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
        needBuild = true;    //谷仓保留,箭塔科技靠它研究
    }
    if (countBuilding(info, BUILDING_FARM) < 2) {
        needBuild = true;    //农田至少2块
    }
    if (gameStage == 3 && findBuilding(info, BUILDING_COLLAGE) < 0) {
        needBuild = true;    //阶段3防守需要学院
    }
    if (!needBuild) {
        //空闲建造工改回无工作,由分配函数重新安排
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
