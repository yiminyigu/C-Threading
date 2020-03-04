#include <iostream>
#include<vector>
#include <atomic>
#include <string>
#include <condition_variable>
#include <thread>
#include <chrono>
#include<unordered_set>
using namespace std;
using namespace chrono;
using namespace chrono_literals;


#define TUNIT microseconds
const static int typeCount=4;
const vector<int> bufferCapa={6,5,4,3};
vector<int> bufferState={0,0,0,0};

const int Required_itNum=5;

std::mutex mtxBuffer;
std::condition_variable produce,consume;
steady_clock::time_point baseTime;

ostream& operator<<(ostream& os, vector<int>& v){
    os<<'(';
    for(int i=0;i<v.size();i++){
        if(i==0){
            os<<v[i];
        }else{
            os<<','<<v[i];
        }
    }
    os<<')';
    return os;
}

//nCr tc O(r)
int nCr(int n, int r)
{
    if(r<0||r>n){
        throw "Invalid "+to_string(r)+"C"+to_string(n);
    }
    if(r==0||r==n){
        return 1;
    }
    
    int ans=1;
    if(r>n/2){
        r=n-r;
    }
    for(int i=0;i<r;i++){
        ans*=(n-i);
        ans/=(i+1);//everytime we get nC1, nC2, nC3,...
    }
    return ans;
}
  

//tc: O(max(S,T)+KlogK), extra sc:O(K), S is pieceCount, K is limitTypeCount, T is totalTypeCount
vector<int> nextSPieceKTypeComboOutOfTType(int pieceCount, int limitTypeCount, int totalTypeCount){
    
    //first reservoir sampling to pick limitTypeCount out of totalTypeCount
    vector<int> ans(totalTypeCount,0);
    srand(time(0));
    vector<int> reservior1;//sc O(K)
    //tc O(T)
    for(int i=0;i<limitTypeCount;i++){
        reservior1.push_back(i);
    }
    for(int i=limitTypeCount;i<totalTypeCount;i++){
        int randNum=rand()%i;
        if( randNum<= limitTypeCount-1){
            reservior1[randNum]=i;
        }
    }
    //tc O(KlogK)
    sort(reservior1.begin(), reservior1.end());
    
    //second reservoir sampling to split pieceCount into limitTypeCountGroup, that is, pick  board limitTypeCount-1 out of pieceCount-1
    vector<int> reservior2(1,-1);//each element represent the board after the value idx,this one for board before idx-0 elem
    if(limitTypeCount>1){
        //tc O(S)
        for(int i=0;i<limitTypeCount-1;i++){
            reservior2.push_back(i);
        }
        
        for(int i=limitTypeCount-1;i<pieceCount-1;i++){
            int randNum=rand()%i;
            if( randNum<= limitTypeCount-2){
                reservior2[randNum+1]=i;
            }
        }
        //tc O(KlogK)
        sort(reservior2.begin(), reservior2.end());
    }
    reservior2.push_back(pieceCount-1);
    //construct ans
    int idx1=0;
    int idx2=1;
    //tc O(T)
    for(int i=0;i<totalTypeCount;i++){
        if(idx1<totalTypeCount && reservior1[idx1]==i){
            idx1++;
            ans[i]=reservior2[idx2]-reservior2[idx2-1];
            idx2++;
        }
        
    }
    
    return ans;
}
//tc O(max(S,T)+KlogK)
//unfortunately this method need to hold the count of whole sample space, which may be a very large number, even larger than RAND_MAX

vector<int> nextSPieceComboOutOfTType(int pieceCount, int totalTypeCount){
    vector<int> ans(totalTypeCount,0);
    if(pieceCount==0)
        return ans;
    
    int minGroupCount=1;
    int maxGroupCount=min(pieceCount,totalTypeCount);
    vector<vector<int>> caseSampleRanges(maxGroupCount+1,vector<int>(2,0));//sc O(max(P,T))
    int comboSampleForSomeLimitTypeCount=1;
    int SplitSampleForSomeLimitTypeCount=1;
    //tc O(max(P,T))
    for(int limitTypeCount=minGroupCount;limitTypeCount<=maxGroupCount;limitTypeCount++){
        if(limitTypeCount==minGroupCount){
            comboSampleForSomeLimitTypeCount=nCr(totalTypeCount, limitTypeCount);
            SplitSampleForSomeLimitTypeCount=nCr(pieceCount-1, limitTypeCount-1);
            caseSampleRanges[limitTypeCount][0]=0;
            caseSampleRanges[limitTypeCount][1]=comboSampleForSomeLimitTypeCount*SplitSampleForSomeLimitTypeCount-1;
        }else{
            comboSampleForSomeLimitTypeCount= comboSampleForSomeLimitTypeCount*(totalTypeCount-(limitTypeCount-1))/limitTypeCount; //we have nC(r-1), to get nCr
            SplitSampleForSomeLimitTypeCount=SplitSampleForSomeLimitTypeCount*(pieceCount-1-(limitTypeCount-2))/(limitTypeCount-1);
            caseSampleRanges[limitTypeCount][0]=caseSampleRanges[limitTypeCount-1][1]+1;
            caseSampleRanges[limitTypeCount][1]=caseSampleRanges[limitTypeCount-1][1]+comboSampleForSomeLimitTypeCount*SplitSampleForSomeLimitTypeCount;
        }
    }
    int sampleSpaceCount=caseSampleRanges.back()[1]+1;
    srand(time(0));
    int ranNum=rand()%sampleSpaceCount;
    int selectedLimitTypeCount=-1;
    //tc O(max(P,T))
    for(int i=1;i<caseSampleRanges.size();i++){
        if(ranNum>=caseSampleRanges[i][0]&&ranNum<=caseSampleRanges[i][1]){
            selectedLimitTypeCount=i;
            break;
        }
    }
    return nextSPieceKTypeComboOutOfTType(pieceCount,selectedLimitTypeCount,totalTypeCount);
}

class PartWorker{
public:
    PartWorker(int id,int itNum):id_(id),itNum_(itNum){
        
    }
    
    void partWorkerWork(){
        vector<string> status={"New Load Order","Wakeup-Notified","Wakeup-Timeout"};
        int statusIdx=0;
        for(int i=0;i<itNum_;i++){
            //produce parts
            produceAndMoveNextLoadOrder();
            
            std::unique_lock<std::mutex> lk(mtxBuffer);
            steady_clock::time_point waitStart;
            steady_clock::time_point expireTIme;
            while(1){
                if(statusIdx==2|| canUpload()){
                    steady_clock::time_point curr=steady_clock::now();
                    //because in this case only thread hold mtxBuffer can cout, we can garantee cout thread safe
                    cout<<"Current Time: "<<duration_cast<TUNIT>(curr-baseTime).count()<<"us"<<endl;
                    cout<<"Part Worker ID: "<<id_<<endl;
                    cout<<"Iteration: "<<i<<endl;
                    cout<<"Status: "<<status[statusIdx]<<endl;
                    auto waitTime=statusIdx==0?0:duration_cast<TUNIT>(curr-waitStart).count();
                    
                    cout<<"Accumulated Wait Time: "<<waitTime<<"us"<<endl;
                    cout<<"Buffer State: "<<bufferState<<endl;
                    cout<<"Load Order: "<<loadOrder_<<endl;
                    
                    bool isComplete = upload();
                    consume.notify_all();
                    
                    cout<<"Updated Buffer State: "<<bufferState<<endl;
                    cout<<"Updated Load Order: "<<loadOrder_<<endl;
                    
                    if(isComplete){
                        break;
                    }
                    if(statusIdx==2){
                        lk.unlock();
                        discard();
                        break;
                    }
                    
                    if(statusIdx==0){
                        statusIdx=1;
                        waitStart=steady_clock::now();
                        expireTIme=waitStart+TUNIT(partWorkerWaitTime_);
                    }
                    if(produce.wait_until(lk,expireTIme)==cv_status::timeout){
                        statusIdx=2;//lk is not locking at this time
                        lk.lock();
                    }
                    
                }
                
            }
        }
    }
    
    void produceAndMoveNextLoadOrder(){
        //
        loadOrder_=nextSPieceComboOutOfTType(partsNumPerProduce_,typeCount);
        producePart();
        movePart();
    }
    
    void producePart(){
        int productTime= 0;
        for(int i=0;i<typeCount;i++){
            productTime+=loadOrder_[i]*produceTime_[i];
        }
        this_thread::sleep_for(TUNIT(productTime));
    }
    
    void movePart(){
        int moveTime= 0;
        for(int i=0;i<typeCount;i++){
            moveTime+=loadOrder_[i]*moveTime_[i];
        }
        this_thread::sleep_for(TUNIT(moveTime));
    }
    
    bool canUpload(){
        //assume caller hold mtxBuffer (any method to check if this::thread hold some mtx??)
        for(int i=0;i<bufferState.size();i++){
            int thisMove=min(loadOrder_[i],bufferCapa[i]-bufferState[i]);
            if(thisMove!=0){
                return true;
            }
        }
        return false;
    }
    
    bool upload(){
        //upload
        bool isCompelete=true;
        for(int i=0;i<bufferState.size();i++){
            int thisMove=min(loadOrder_[i],bufferCapa[i]-bufferState[i]);
            loadOrder_[i]-=thisMove;
            bufferState[i]+=thisMove;
            if(loadOrder_[i]!=0){
                isCompelete=false;
            }
        }
        return isCompelete;
    }
    
    
    void discard(){
        int discardTime=0;
        for(int i=0;i<loadOrder_.size();i++){
            discardTime+=loadOrder_[i]*moveTime_[i];
        }
        this_thread::sleep_for(TUNIT(discardTime));
        //loadOrder_.clear();
    }
    
private:
    int id_;
    int itNum_;
    vector<int> loadOrder_;
    
    const static vector<int> produceTime_;
    const static vector<int> moveTime_;
    
    const static int partsNumPerProduce_=4;
    const static int partWorkerWaitTime_;
};
const int PartWorker::partWorkerWaitTime_=3000;
const vector<int> PartWorker::produceTime_={50,70,90,110};
const vector<int> PartWorker::moveTime_={20,30,40,50};




class ProductWorker{
public:
    ProductWorker(int id,int itNum):id_(id),itNum_(itNum){
        
    }
    
    void productWorkerWork(){
        vector<string> status={"New Pickup Order","Wakeup-Notified","Wakeup-Timeout"};
        int statusIdx=0;
        for(int i=0;i<itNum_;i++){
            nextPickUpOrder();
            
            vector<int> originOrder=pickUpOrder_;
            std::unique_lock<std::mutex> lk(mtxBuffer);
            steady_clock::time_point waitStart;
            steady_clock::time_point expireTIme;
            while(1){
                if(statusIdx==2|| canGrab()){
                    steady_clock::time_point curr=steady_clock::now();
                    //because in this case only thread hold mtxBuffer can cout, we can garantee cout thread safe
                    cout<<"Current Time: "<<duration_cast<TUNIT>(curr-baseTime).count()<<"us"<<endl;
                    cout<<"Product Worker ID: "<<id_<<endl;
                    cout<<"Iteration: "<<i<<endl;
                    cout<<"Status: "<<status[statusIdx]<<endl;
                    auto waitTime=statusIdx==0?0:duration_cast<TUNIT>(curr-waitStart).count();
                    
                    cout<<"Accumulated Wait Time: "<<waitTime<<"us"<<endl;
                    cout<<"Buffer State: "<<bufferState<<endl;
                    cout<<"Pickup Order: "<<pickUpOrder_<<endl;
                    
                    bool isGrabComplete = grab();
                    produce.notify_all();
                    
                    cout<<"Updated Buffer State: "<<bufferState<<endl;
                    cout<<"Updated Pickup Order: "<<pickUpOrder_<<endl;
                    
                    if(isGrabComplete){
                        std::unique_lock<std::mutex> lk(mtxTotalProds);//this lock is unneccessary in this case because only when productWorker get buffer lock can he modify totalProds
                        cout<<"Total Completed Products: "<<++totalProds<<endl;
                        moveAndAssembleProduct();//simple sleep function for move and assemble without modify totalProd
                        break;
                    }
                    //other case, do not need to modify totalProds
                    std::unique_lock<std::mutex> lk(mtxTotalProds);//this lock is unneccessary in this case because only when productWorker get buffer lock can he modify totalProds
                    cout<<"Total Completed Products: "<<++totalProds<<endl;
                    
                    if(statusIdx==2){
                        lk.unlock();
                        discard(originOrder);
                        break;
                    }
                    
                    if(statusIdx==0){
                        statusIdx=1;
                        waitStart=steady_clock::now();
                        expireTIme=waitStart+TUNIT(productWorkerWaitTime_);
                    }
                    if(consume.wait_until(lk,expireTIme)==cv_status::timeout){
                        statusIdx=2;//lk is not locking at this time
                        lk.lock();
                    }
                    
                }
                
            }
            
        }
    }
    
    
    void nextPickUpOrder(){//assume that typeCount>=typeNumForAssembly,pieceNumForAssembly>=typeNumForAssembly
        pickUpOrder_=nextSPieceKTypeComboOutOfTType(pieceNumForAssembly,typeNumForAssembly,typeCount);
    }
    
    bool canGrab(){
        for(int i=0;i<bufferState.size();i++){
            int thisMove=min(pickUpOrder_[i],bufferState[i]);
            if(thisMove!=0){
                return true;
            }
        }
        return false;
    }
    
    bool grab(){//return is pickup complete
        int isGrabComplete=true;
        for(int i=0;i<bufferState.size();i++){
            int thisMove=min(pickUpOrder_[i],bufferState[i]);
            pickUpOrder_[i]-=thisMove;
            bufferState[i]+=thisMove;
            if(pickUpOrder_[i]!=0){
                isGrabComplete=false;
            }
        }
        return isGrabComplete;
    }
    
    void moveParts(){
        int moveTime= 0;
        for(int i=0;i<typeCount;i++){
            moveTime+=pickUpOrder_[i]*moveTime_[i];
        }
        this_thread::sleep_for(TUNIT(moveTime));
    }
    
    void moveAndAssembleProduct(){
        moveParts();
        int assembleTime= 0;
        for(int i=0;i<typeCount;i++){
            assembleTime+=pickUpOrder_[i]*assembleTime_[i];
        }
        this_thread::sleep_for(TUNIT(assembleTime));
//        std::unique_lock<std::mutex> lk(mtxTotalProds);
//        totalProds++;
//        return totalProds;
    }
    
    void discard(vector<int>& originOrder){
        int discardTime=0;
        for(int i=0;i<pickUpOrder_.size();i++){
            discardTime+=(originOrder[i]- pickUpOrder_[i])*moveTime_[i];
        }
        this_thread::sleep_for(TUNIT(discardTime));
    }
    
    
private:
    int id_;
    int itNum_;
    vector<int> pickUpOrder_;
    
    static int totalProds;
    static mutex mtxTotalProds;
    
    const static int pieceNumForAssembly=5;
    const static int typeNumForAssembly=3;
    const static int productWorkerWaitTime_;
    
    const static vector<int> moveTime_;
    const static vector<int> assembleTime_;
};
int ProductWorker::totalProds=0;
mutex ProductWorker::mtxTotalProds;

const int ProductWorker::productWorkerWaitTime_=6000;
const vector<int> ProductWorker::assembleTime_={80,100,120,140};
const vector<int> ProductWorker::moveTime_={20,30,40,50};




int main(){
    const int m = 20, n = 16; //m: number of Part Workers
    //n: number of Product Workers //m>n
    vector<PartWorker> parws;
    for(int i=0;i<m;i++){
        parws.push_back(PartWorker(i,Required_itNum));
    }
    vector<ProductWorker> prows;
    for(int i=0;i<n;i++){
        prows.push_back(ProductWorker(i,Required_itNum));
    }
    
    thread partW[m]; thread prodW[n];
    for (int i = 0; i < n; i++){
        partW[i] = thread(&PartWorker::partWorkerWork, ref(parws[i]));
        prodW[i] = thread(&ProductWorker::productWorkerWork, ref(prows[i]));
    }
    
    for (int i = n; i<m; i++) {
        partW[i] = thread(&PartWorker::partWorkerWork, ref(parws[i]));
    }
    /* Join the threads to the main threads */
    for (int i = 0; i < n; i++) {
        partW[i].join();
        prodW[i].join();
    }
    for (int i = n; i<m; i++) {
        partW[i].join();
    }
        cout << "Finish!" << endl;
        return 0;
        
}

