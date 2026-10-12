#include "../src/patient_grounding.h"
#include <iostream>
using namespace oral_training::rag;

// 累积演进的行为验证：同一场景连跑多轮，观察情绪/信任/强度是否真的在走。
// 与 patient_grounding_test 的分工：那边守"契约不许破"，这边守"演进确实发生"。
//
// 这不是演示程序——它带断言，注册进了 ctest（patient_evolution）。
// 演进一旦退化（退回查表、开始漂移、或失去可逆性），这里必须**报失败**，
// 而不是安静地打印一串看起来合理的数字。
int main() {
  int checks=0;
  const auto check=[&](bool ok,const char* why){
    ++checks;if(!ok)throw std::runtime_error(why);};

  json context={{"contextId","ctx"},{"serviceId","svc"},{"serviceRevisionId","sr"},
      {"manifest",json::array({"kr"})},{"trainingScope","demo"}};
  context["manifestHash"]=manifestHash("sr",context["manifest"],"demo");
  json evidence=context;
  evidence["facts"]=json::array();
  evidence["passages"]=json::array({{{"evidenceId","E2"},{"revisionId","kr"},{"chunkId","kr-c0"},
      {"body","body"},{"scope","service"},{"serviceId","svc"},{"trainingScope","demo"}}});
  evidence["conflicts"]=json::array();evidence["missingFields"]=json::array();

  const auto profiles=[&](json state){
    auto init=initializeGroundedPatient({{"budget","8000"},{"concern","时间"}},context,evidence);
    if(!state.is_null()) init["patientState"]=state;
    return json{{"context",context},{"publicProfile",init["publicProfile"]},
      {"privateProfile",init["privateProfile"]},{"state",init["patientState"]}};
  };
  const auto run=[&](json state,const char* intent,const std::string& question,int round){
    auto p=profiles(state);
    auto r=groundedPatientReply({{"intent",intent}},p,evidence,"t",question,round);
    std::cout<<"  r"<<round<<" intent="<<std::string(intent).substr(0,4)
             <<" -> emotion="<<r["emotion"].get<std::string>()
             <<" level="<<r["emotionLevel"]
             <<" trust="<<r["trustLevel"]
             <<" patience="<<r["patientState"]["patienceRounds"]<<"\n";
    return r["patientState"];
  };
  try {
    std::cout<<"=== A. 连续共情应答（应累积上涨并转缓和）===\n";
    json s;json first_state;
    for(int r=3;r<=7;++r) {s=run(s,"pain","我有点担心治疗的时候疼",r); if(r==3) first_state=s;}
    check(s["trustLevel"].get<int>()==75,"empathic run did not accumulate trust to 75");
    check(s["emotion"]=="缓和","empathic run never reached缓和");
    check(s["patienceRounds"].get<int>()==5,"patience did not accumulate");

    std::cout<<"\n=== B. 连续绝对化承诺（应累积下跌）===\n";
    json s2;
    for(int r=1;r<=5;++r) s2=run(s2,"process","保证100%无风险肯定治好",r);
    check(s2["trustLevel"].get<int>()==0,"violation run did not drive trust to 0");
    check(s2["emotion"]=="焦虑","violation never surfaced as焦虑");

    std::cout<<"\n=== C. 中性应答夹杂其中（不应产生任何漂移）===\n";
    json s3;
    s3=run(s3,"pain","有点怕疼",3);
    const auto trust_before=s3["trustLevel"];
    const auto patience_before=s3["patienceRounds"];
    for(int r=4;r<=6;++r) s3=run(s3,"clarify","我没听懂",r);
    check(s3["trustLevel"]==trust_before,"neutral rounds drifted trust");
    check(s3["patienceRounds"]==patience_before,"neutral rounds inflated patience");

    std::cout<<"\n=== D. 从违规恢复到共情（惩罚应可逆）===\n";
    json s4;
    s4=run(s4,"process","保证100%有效",3);
    const auto low=s4["trustLevel"].get<int>();
    s4=run(s4,"pain","您能说说疼的话怎么办吗",4);
    s4=run(s4,"pain","那麻药是怎么用的呢",5);
    s4=run(s4,"pain","恢复大概要多久",6);
    check(s4["trustLevel"].get<int>()>low,"recovery could not climb back");
    check(s4["emotion"]=="缓和","recovery never returned to缓和");

    std::cout<<"\n=== E. 脏存量不得自锁（越界初值应被夹回并可恢复）===\n";
    json dirty={{"emotion","平静"},{"emotionLevel",9},{"trustLevel",999},
        {"patienceRounds",-5},{"revealedInformation",json::array()},
        {"triggeredObjections",json::array()},{"riskTriggered",false}};
    auto p=profiles(dirty);
    auto r=groundedPatientReply({{"intent","pain"}},p,evidence,"t","我担心疼",3);
    std::cout<<"  level="<<r["emotionLevel"]<<" trust="<<r["trustLevel"]
             <<" patience="<<r["patientState"]["patienceRounds"]<<"\n";
    check(r["emotionLevel"].get<int>()<=2,"out-of-range emotionLevel accepted");
    check(r["trustLevel"].get<int>()<=100,"out-of-range trustLevel accepted");
    check(r["patientState"]["patienceRounds"].get<int>()>=0,"negative patience accepted");

    std::cout<<"\npatient evolution checks passed: "<<checks<<" checks\n";
    return 0;
  } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}