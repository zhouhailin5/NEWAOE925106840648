#ifndef USRAI_H
#define USRAI_H

#include "ai.h"
#include <unordered_map>
#include <set>

extern tagGame tagUsrGame;
extern ins UsrIns;
/*##########DO NOT MODIFY THE CODE ABOVE##########*/

class UsrAI :public AI
{
public:
    UsrAI() { this->id = 0; }
    ~UsrAI() {}

private:
    void processData() override;
    tagInfo getInfo() { return tagUsrGame.getInfo(); }
    int AddToIns(instruction ins) override
    {
        UsrIns.lock.lock();
        ins.id = UsrIns.g_id;
        UsrIns.g_id++;
        UsrIns.instructions.push(ins);
        UsrIns.lock.unlock();
        return ins.id;
    }
    void clearInsRet() override
    {
        tagUsrGame.clearInsRet();
    }
    /*##########DO NOT MODIFY THE CODE IN THE CLASS##########*/




/*##########YOUR CODE BEGINS HERE##########*/
  //每帧要做的事情,processData会调用它
    void strategyMain(const tagInfo& info);

    //把地图信息更新到数组里,方便找空地建房子
    void updateMapInfo(const tagInfo& info);

    // 祭司智能行为（融合初始移动和动态逃跑/回塔）
    void priestBehavior(const tagInfo& info);

    // 祭司开局探路:第一波到来前,依次访问 第二近->第四近->第三近->中心点(50,50)
    void priestExplore(const tagInfo& info);

    // 在初始箭塔周围额外建造箭塔
    void buildExtraTowers(const tagInfo& info);

    //给空闲的村民分配工作(砍树、采浆果、挖石头等)
    void assignWork(const tagInfo& info);

    //派一个村民去盖房子
    void buildHouse(const tagInfo& info);

    //获取选址基准点:军事建筑(category=1)→开局箭塔,其余→开局房屋;均没有则回退市镇中心
    void getBuildBase(int category, int& bx, int& by);

    //派一个村民去盖其他建筑(市场、兵营、靶场、马厩、箭塔、学院等)
    void buildSomeBuilding(const tagInfo& info, int buildingType);

    //派一个村民去盖农田(食物稳定,不然后期没东西吃)
    void buildFarm(const tagInfo& info);

    //研究各种科技
    void researchTech(const tagInfo& info);

    //让市镇中心生产村民
    void makeVillager(const tagInfo& info);

    //让市镇中心升级时代(从工具时代升到铜器时代)
    void upgradeAge(const tagInfo& info);

    //让兵营、靶场、马厩、学院生产士兵
    void makeArmy(const tagInfo& info);

    //指挥军队攻击看到的敌人
    void armyFight(const tagInfo& info);

    //箭塔自动攻击射程内的敌人
    void towerFight(const tagInfo& info);

    /* ================== 下面是一些辅助函数 ================== */

    //找祭司的编号和位置(找不到返回false)
    bool findPriest(const tagInfo& info, int& sn, int& bx, int& by, double& dr, double& ur);

    //找祭司的安全点:有箭塔去箭塔旁,没有就去市镇中心旁(找不到返回false)
    bool findPriestSafeSpot(const tagInfo& info, int& x, int& y);

    //找某类资源对应的存放点(浆果/农田食物→谷仓,木/石/金/猎物→仓库,都没有→市镇中心),找到返回true
    bool findDropoff(const tagInfo& info, int resType, int& bx, int& by);

    //找距离存放点最近的某类资源SN,找不到返回-1;exclude里的SN跳过(本帧已被别人选的资源)
    //采集效率优化:按"资源到存放点的距离"选,村民返程交资源的路程最短
    int findResourceNearDropoff(const tagInfo& info, int resType, const std::set<int>& exclude);

    //找到第一个已经建好的某种建筑的SN,找不到返回-1
    int findBuilding(const tagInfo& info, int buildType);

    //数一数某种建筑有几个(包括没建好的)
    int countBuilding(const tagInfo& info, int buildType);

    //数一数某种士兵有几个
    int countArmy(const tagInfo& info, int armyType);

    //判断某建筑是否正在忙(Project不为0)
    bool isBuildingBusy(const tagInfo& info, int sn);

    //找空闲村民,优先之前的建造工,其次任意空闲村民;找不到返回-1
    int findIdleWorker(const tagInfo& info);

    //判断(x,y)这个位置能不能放下size*size的建筑
    bool canBuildHere(const tagInfo& info, int x, int y, int size);

    //以(centerX,centerY)为中心,向外一圈一圈找可以放建筑的空地
    bool findBuildPlace(const tagInfo& info, int& x, int& y, int size, int centerX, int centerY);

    //计算两个块坐标之间的距离
    double distanceBlock(int x1, int y1, int x2, int y2);

    //判断某个单位能不能下指令(防止每帧重复下同一个指令)
    bool canOrder(int sn, int gap);

    //记录某个单位下指令的时间
    void rememberOrder(int sn);

    //返回一格的长度
    double blockLength();

    //把块坐标转成细节坐标(取格子的中心点)
    double detailOf(int block);
};




/*##########YOUR CODE ENDS HERE##########*/
#endif // USRAI_H
