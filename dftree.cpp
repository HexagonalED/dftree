#include <iostream>
#include <thread>
#include <cstdlib>
#include <random>
#include <atomic>
#include <chrono>
#include <ctime>

//#include "Exchanger.h"

#define TIMEOUT 100/*ms*/

#define EMPTY 0
#define WAITING 1
#define BUSY 2
#define TIMEOUTEXCEPTION -1
//#include <boost/thread.hpp>

using namespace std;
using namespace chrono;

int* location;


class pidStatusPair{
  public:
    int pid;
    int status;

    pidStatusPair():pid(-1),status(EMPTY){
    }

    pidStatusPair(int p,int s):pid(p),status(s){
    }
};

class valExchanger{
  public:
    atomic<pidStatusPair> slot;
    
    valExchanger():slot(*(new pidStatusPair(-1,EMPTY))){
    }

    void exchangerInit(){
      pidStatusPair ps(-1,EMPTY);
      slot=ps;
    }
    int exchange(int input, long timeout){
      auto milliTimeout = nanoseconds(timeout);
      auto now_ms = duration_cast<nanoseconds>(time_point_cast<nanoseconds>(system_clock::now()).time_since_epoch());
      auto timeBound = now_ms+milliTimeout;
      //cout<<"TIMECONSTRATIN[now : "<<now_ms.count()<<", timeBound : "<<timeBound<<"]"<<endl;
      while(true){
//        //cout<<"in loop"<<endl;
        auto newNow_ms = duration_cast<nanoseconds>(time_point_cast<nanoseconds>(system_clock::now()).time_since_epoch());
        if(newNow_ms>timeBound){
          //cout<<"pid miss!"<<endl;
          throw TIMEOUTEXCEPTION;
        }
        pidStatusPair slotValue = slot.load();
        pidStatusPair desired_WAITING(input,WAITING);
        pidStatusPair desired_BUSY(input,BUSY);
        pidStatusPair initVal(-1,EMPTY);
        int stat = slotValue.status;
        switch(stat){
          case EMPTY:
            //cout<<"empty"<<endl;
            if(slot.compare_exchange_weak(slotValue,desired_WAITING)){
              //cout<<"cased_empty"<<endl;
            //if(slot.compare_exchange_weak(slotValue,input) && status.compare_exchange_weak(stat,WAITING)){
              while(duration_cast<nanoseconds>(time_point_cast<nanoseconds>(system_clock::now()).time_since_epoch())<timeBound){
                 //cout<<"while not out of bound_empty("<<duration_cast<milliseconds>(time_point_cast<milliseconds>(now).time_since_epoch()).count()<<")boundto("<<timeBound<<")"<<endl;
                int val = slot.load().pid;
                int valStat = slot.load().status;
                if(valStat ==BUSY){
                  slot=initVal;
                  return val;
                }
              }
              int wVal=WAITING;
              if(slot.compare_exchange_weak(desired_WAITING,initVal)){
              //if(slot.compare_exchange_weak(input,-1)&&status.compare_exchange_weak(wVal,EMPTY)){
                //cout<<"timeout_waiting"<<endl;
                throw TIMEOUTEXCEPTION;
              }else{
                //cout<<"cas not work, return val"<<endl;
                int val = slot.load().pid;
                slot=initVal;
                return val;
              }
            }
            break;
          case WAITING:
            //cout<<"waiting"<<endl;
            if(slot.compare_exchange_weak(slotValue,desired_BUSY)){
              //cout<<"cased_waiting"<<endl;
            //if(slot.compare_exchange_weak(slotValue,input) && status.compare_exchange_weak(stat,BUSY)){
              return slotValue.pid;
            }
            break;
          case BUSY:
            break;
          default:
            break;
        }
      }
    }
};
class prism{
  public :
    valExchanger *slots;
    //  atomic<int> *slots;
    int width;
    prism(int w){
      width = w;
      slots = new valExchanger[width];
      //slots = new atomic<int>[width];
    }
    bool swap(int mypid){
      int randLoc = rand()%width;
      //int ret = slots[randLoc].exchange(mypid);
      int ret = slots[randLoc].exchange(mypid,TIMEOUT);
      //cout<<"swap:"<<ret<<","<<mypid<<endl;
      //cout<<"retval in prism swap|"<<mypid<<","<<ret<<endl;
      //cout<<"mypid:"<<mypid<<",ret:"<<ret<<",bool:"<<(mypid<ret)<<endl;
      return (mypid<ret);
    }
};

class balancer{
  public:
    atomic<bool> toggle;
    balancer(){
      toggle = true;
    }
    int traverse(){
      if(toggle){
        toggle.exchange(!toggle);
        return 0;
      }else{
        toggle.exchange(!toggle);
        return 1;
      }
    }
};

class dfBalancer{
  public:
    prism* p;
    balancer* toggle;

    dfBalancer(int width){
      p = new prism(width);
      toggle = new balancer();
    }
    int traverse(int mypid){
      try{
        if(p->swap(mypid)){
          return 0;
        }else{
          return 1;
        }
      }catch(int e){
        return toggle->traverse();
      }
    }
};

class dfTree{
  public:
    dfBalancer* root;
    dfTree* next;
    //atomic<int>* leaf_counter;
    int width;
    dfTree(int w){
      width = w;
      root = new dfBalancer(width);
      if(width>2){
        next = new dfTree[2]{width/2,width/2};
      }
    }
    int traverse(int mypid){
      int half = root->traverse(mypid);
      if(width>2){
        return (2*(next[half].traverse(mypid))+half);
      }else{
        return half;
      }
    }
};


void run(dfTree* t,int pid, int nInput, atomic<int>* lf){
  int ret =-1;
  for(int i=0;i<nInput;i++){
    ret = t->traverse(pid);
    lf[ret]++;
  }
}





int main(){
  int width[4]={4,8,16,32};
  int nThread[4]={18,36,72,144};
  int nToken = 1000000;
  srand((unsigned int)time(0));
  for(int w=0;w<4;w++){
    for(int t=0;t<4;t++){
      cout<<"width : "<<width[w]<<", thread : "<<nThread[t]<<endl;
      thread* thrd = new thread[nThread[t]];
      dfTree* tree = new dfTree(width[w]);
      atomic<int>* leafCounters = new atomic<int>[width[w]];
      for(int i=0;i<width[w];i++){
        leafCounters[i]=0;
      }
      int nInput=nToken/nThread[t];

      auto start = system_clock::now();

      for(int i=0;i<nThread[t];i++){
        thrd[i]=thread(run,tree,i,nInput,leafCounters);
      }
      for(int i=0;i<nThread[t];i++){
        thrd[i].join();
      }
      auto end = system_clock::now();
      duration<float> elapsed = end-start;
      int result=0;
      for(int i=0;i<width[w];i++){
        result+=leafCounters[i].load();
      }
      cout<<"result : "<<result<<endl;
//      cout<<"Timestart : "<<duration_cast<milliseconds>(end-begin).count()/1000000.0<<endl;
//      cout<<"Timeend : "<<duration_cast<milliseconds>(end-begin).count()/1000000.0<<endl;

//      cout<<"Timestart : "<<start.count()<<endl;
//      cout<<"Timeend : "<<end.count()<<endl;
      cout<<"ELAPSED:"<<elapsed.count()<<endl;
      delete tree;
      delete leafCounters;
    }
  }
  return 0;
}





