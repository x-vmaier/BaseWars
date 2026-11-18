// Naval/ferry/path-line test bot. Usage: ships_bot <uuid> <name> <mode> [port]   modes: 1 naval basics, 2 ferry, 3/4/5 path-line visibility (clan founder / clan joiner / stranger)
// Headless protocol test bot (see the scenario modes in main). Usage: testbot <uuid> <name> <mode> [port]
#include "shared.hpp"
#include <cstdio>
#include <map>
#include <set>
#include <unordered_map>
#include <thread>
#include <chrono>
struct E{uint32_t id;Kind k;int o;float x,y,hp,mhp;int st,aux,aim,prog,stock;};
std::unordered_map<int64_t,std::array<uint8_t,256>> terr; std::unordered_map<int64_t,std::array<uint8_t,32>> fog;
struct ShipI{uint32_t uid;int tier,kinds[5],res[4],voy;}; std::vector<ShipI> shipInfo; std::map<uint32_t,std::vector<IV>> paths; int pathUpd=0;
std::map<uint32_t,E> ents; float res[4]; int rep[5]; int ulv[5]; int me=0; std::vector<std::string> texts; std::string rosterS;
void pk(const uint8_t*d,size_t n){ Reader r{d,d+n}; switch(r.get<uint8_t>()){
 case S_Welcome: me=r.get<uint8_t>(); printf("welcome id=%d seed=%u cam=%.1f,%.1f name=%s\n",me,r.get<uint32_t>(),r.get<float>(),r.get<float>(),r.str().c_str());break;
 case S_State:{ for(float&v:res)v=r.get<float>(); for(int f=0;f<5;f++)rep[f]=r.get<int8_t>(); for(int f=0;f<5;f++)ulv[f]=r.get<uint8_t>(); int c=r.get<uint16_t>(); ents.clear(); for(int i=0;i<c;i++){E e;e.id=r.get<uint32_t>();e.k=Kind(r.get<uint8_t>());e.o=r.get<uint8_t>();e.x=r.get<float>();e.y=r.get<float>();e.hp=r.get<uint16_t>();e.mhp=r.get<uint16_t>();e.st=r.get<uint8_t>();e.aux=r.get<uint8_t>();e.aim=r.get<uint8_t>();e.prog=r.get<uint8_t>();e.stock=r.get<uint16_t>();ents[e.id]=e;} break;}
 case S_Fog:{ int nt=r.get<uint16_t>(); for(int i=0;i<nt;i++){int cx=r.get<int32_t>(),cy=r.get<int32_t>();auto b=r.bytes(256);std::array<uint8_t,256>a;memcpy(a.data(),b,256);terr[chunkKey(cx,cy)]=a;} int nf=r.get<uint16_t>(); for(int i=0;i<nf;i++){int cx=r.get<int32_t>(),cy=r.get<int32_t>();auto b=r.bytes(32);std::array<uint8_t,32>a;memcpy(a.data(),b,32);fog[chunkKey(cx,cy)]=a;} break;}
 case S_Text: {std::string s=r.str(); printf("  [text] %s\n",s.c_str()); break;}
 case S_Roster:{int n=r.get<uint8_t>(); rosterS.clear(); for(int i=0;i<n;i++){int id=r.get<uint8_t>(),fl=r.get<uint8_t>(),cl=r.get<uint8_t>();std::string nm=r.str();int has=r.get<uint8_t>();float x=r.get<float>(),y=r.get<float>();rosterS+=nm+"("+std::to_string(id)+",f"+std::to_string(fl)+",clan"+std::to_string(cl)+(has?",loc "+std::to_string(int(x))+","+std::to_string(int(y)):"")+") ";} break;}
 case S_Chat:{int ch=r.get<uint8_t>(),sd=r.get<uint8_t>();std::string nm=r.str(),tx=r.str();printf("  [chat %d] %s: %s\n",ch,nm.c_str(),tx.c_str());break;}
 case S_Paths:{int n=r.get<uint16_t>(); for(int i=0;i<n;i++){uint32_t id=r.get<uint32_t>();int np=r.get<uint8_t>(); std::vector<IV> v; for(int j=0;j<np;j++){int x=r.get<int32_t>(),y=r.get<int32_t>();v.push_back({x,y});} pathUpd++; if(np) paths[id]=v; else paths.erase(id); if(!r.ok)break;} break;}
 case S_ShipInfo:{int n=r.get<uint8_t>(); shipInfo.clear(); for(int i=0;i<n;i++){ShipI s; s.uid=r.get<uint32_t>();s.tier=r.get<uint8_t>();for(int&k:s.kinds)k=r.get<uint8_t>();for(int&q:s.res)q=r.get<uint16_t>();s.voy=r.get<uint8_t>();shipInfo.push_back(s);} break;}
 default:break;}}

ENetHost*host;ENetPeer*peer;
void pump(int ms){ auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms); while(std::chrono::steady_clock::now()<end){ENetEvent ev; while(enet_host_service(host,&ev,10)>0){ if(ev.type==ENET_EVENT_TYPE_RECEIVE){pk(ev.packet->data,ev.packet->dataLength);enet_packet_destroy(ev.packet);} } } }
void snd(const Writer&w){sendPacket(peer,w);enet_host_flush(host);}
struct PW{ int biome(int x,int y){auto it=terr.find(chunkKey(chunkOf(x),chunkOf(y)));return it==terr.end()?INVALID_BIOME:it->second[localIdx(x,y)];}
 bool explored(int x,int y){auto it=fog.find(chunkKey(chunkOf(x),chunkOf(y)));if(it==fog.end())return false;int i=localIdx(x,y);return it->second[i>>3]>>(i&7)&1;}
 bool occupied(int x,int y){for(auto&[i,e]:ents){if(!isBuilding(e.k))continue;int tx=std::lround(e.x-KINFO[e.k].fw*.5f),ty=std::lround(e.y-KINFO[e.k].fh*.5f);if(x>=tx&&x<tx+KINFO[e.k].fw&&y>=ty&&y<ty+KINFO[e.k].fh)return true;}return false;}
 template<class F> void anchors(F f){for(auto&[i,e]:ents)if(e.k==K_TownHall||e.k==K_Outpost)f(Pos{e.x,e.y},e.k,e.o==me,false);} };
bool place(Kind k,float cx,float cy,int r0,int r1){ PW w; for(int r=r0;r<=r1;r++)for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++){ if(std::max(abs(dx),abs(dy))!=r)continue; int tx=(int)cx+dx,ty=(int)cy+dy; if(checkPlace(w,k,tx,ty))continue; Writer b;b.put<uint8_t>(C_Build).put<uint8_t>(k).put<int32_t>(tx).put<int32_t>(ty);snd(b);pump(400);printf("placed %s at %d,%d\n",KINFO[k].name,tx,ty);return true;} printf("no tile for %s\n",KINFO[k].name); return false;}
uint32_t firstOf(Kind k){for(auto&[i,e]:ents)if(e.o==me&&e.k==k)return i;return 0;}
std::vector<uint32_t> ships(){std::vector<uint32_t> v;for(auto&[i,e]:ents)if(e.o==me&&e.k==K_Ship)v.push_back(i);return v;}
std::vector<uint32_t> troops(){std::vector<uint32_t> v;for(auto&[i,e]:ents)if(e.o==me&&isTroop(e.k)&&!(e.st&0x80))v.push_back(i);return v;}
int aboardCount(){int n=0;for(auto&[i,e]:ents)if(e.o==me&&isTroop(e.k)&&e.prog==255)n++;return n;}
void board(const std::vector<uint32_t>&us,uint32_t ship){Writer w;w.put<uint8_t>(C_Board).put<uint8_t>(uint8_t(us.size()));for(auto u:us)w.put<uint32_t>(u);w.put<uint32_t>(ship);snd(w);}
void moveU(const std::vector<uint32_t>&us,float x,float y){Writer w;w.put<uint8_t>(C_Move).put<uint8_t>(uint8_t(us.size()));for(auto u:us)w.put<uint32_t>(u);w.put<float>(x).put<float>(y);snd(w);}
void train(uint32_t b,Kind k){Writer t;t.put<uint8_t>(C_Train).put<uint32_t>(b).put<uint8_t>(k);snd(t);pump(300);}
void shipLine(const char*tag){for(auto id:ships()){auto&e=ents[id];printf("[%s] ship %u tier(aux)=%d troops(prog)=%d res(stock)=%d at %.1f,%.1f hp %.0f/%.0f\n",tag,id,e.aux,e.prog,e.stock,e.x,e.y,e.hp,e.mhp);}}
int main(int argc,char**argv){ std::string uuid=argc>1?argv[1]:"bot-uuid-0000001"; std::string name=argc>2?argv[2]:"Bot"; int mode=argc>3?atoi(argv[3]):1;
 setvbuf(stdout,nullptr,_IOLBF,0);
 enet_initialize(); host=enet_host_create(nullptr,1,2,0,0); ENetAddress a; enet_address_set_host(&a,"127.0.0.1"); a.port=argc>4?uint16_t(atoi(argv[4])):7803; peer=enet_host_connect(host,&a,2,0);
 pump(500); Writer h; h.put<uint8_t>(C_Hello); h.str(uuid); h.str(name); h.put<uint64_t>(0); h.put<uint16_t>(0); snd(h); pump(2500);
 uint32_t th=firstOf(K_TownHall); E te=ents[th];
 printf("TH uid=%u at %.1f,%.1f res W%.0f G%.0f S%.0f M%.0f ents=%zu\n",th,te.x,te.y,res[0],res[1],res[2],res[3],ents.size());
 if(mode==1){
  place(K_Harbour,te.x-6,te.y,0,6); place(K_Archery,te.x,te.y-7,0,6);
  pump(33000);
  uint32_t hb=firstOf(K_Harbour), ar=firstOf(K_Archery); printf("harbour %u (complete prog=%d) archery %u prog=%d\n",hb,ents[hb].prog,ar,ents[ar].prog);
  train(hb,K_Ship); pump(1500); shipLine("built L1 ship"); { auto s=ships(); if(s.empty()){puts("FAIL no ship");return 1;} printf("EMPTY? troops aboard=%d (expect 0)\n",aboardCount()); }
  for(int i=0;i<8;i++)train(ar,K_Warrior);
  pump(500); auto tr=troops(); printf("troops available: %zu (L1 ship cap 6)\n",tr.size());
  uint32_t s1=ships()[0];
  board(tr,s1); pump(25000); shipLine("after boarding 11 on L1"); printf("aboard (prog==255) = %d, expect 6\n",aboardCount());
  // boarded units must not be commandable
  { std::vector<uint32_t> ab; for(auto&[i,e]:ents) if(e.o==me&&e.prog==255) ab.push_back(i); if(!ab.empty()){ float x0=ents[ab[0]].x; moveU(ab,te.x,te.y+10); pump(1500); printf("aboard unit command ignored: x %.1f -> %.1f, still aboard=%d\n",x0,ents[ab[0]].x,ents[ab[0]].prog==255);} }
  if(!shipInfo.empty()){ auto&s=shipInfo[0]; printf("S_ShipInfo: ship %u tier %d kinds P%d W%d A%d L%d M%d\n",s.uid,s.tier,s.kinds[0],s.kinds[1],s.kinds[2],s.kinds[3],s.kinds[4]); }
  // sail out and back
  float sx=ents[s1].x, sy=ents[s1].y; moveU({s1},sx-10,sy); pump(7000); shipLine("sailed west"); 
  { Writer d; d.put<uint8_t>(C_Disembark).put<uint32_t>(s1); snd(d); pump(1200); printf("disembark at sea: aboard=%d (expect still 6, coast far)\n",aboardCount()); }
  moveU({s1},sx,sy); pump(8000); shipLine("back at harbour");
  { Writer d; d.put<uint8_t>(C_Disembark).put<uint32_t>(s1); snd(d); pump(1500); printf("after disembark at coast: aboard=%d (expect 0)\n",aboardCount());
    for(auto&[i,e]:ents) if(e.o==me&&isTroop(e.k)) printf("  unit %u %s at %.1f,%.1f prog=%d biome=%d\n",i,KINFO[e.k].name,e.x,e.y,e.prog,PW().biome((int)std::floor(e.x),(int)std::floor(e.y))); }
  // re-embark a subset
  { auto t2=troops(); t2.resize(std::min<size_t>(3,t2.size())); board(t2,s1); pump(15000); printf("re-embark 3: aboard=%d (expect 3)\n",aboardCount()); shipLine("re-embarked"); }
  // tier 2
  { Writer u; u.put<uint8_t>(C_Upgrade).put<uint32_t>(hb); snd(u); pump(800); train(hb,K_Ship); pump(1000); for(auto id:ships()) printf("ship %u tier=%d\n",id,ents[id].aux);
    auto sh=ships(); uint32_t s2=sh.back(); auto t3=troops(); printf("troops on land: %zu\n",t3.size()); board(t3,s2); pump(25000); shipLine("after boarding on L2"); }
  enet_peer_disconnect(peer,0); pump(300); return 0; }
 if(mode==2){
  place(K_Harbour,te.x-6,te.y,0,6);
  pump(28000); uint32_t hb=firstOf(K_Harbour); train(hb,K_Ship); pump(1000); auto sh=ships(); if(sh.empty()){puts("FAIL no ship");return 1;}
  uint32_t s1=sh[0]; moveU({s1},te.x-18,te.y); pump(9000); shipLine("scouting");   // reveals the far shore
  Writer d; place(K_Outpost,te.x-25,te.y,0,6); place(K_Lumber,te.x-25,te.y,3,8);
  pump(24000); uint32_t op=0; for(auto&[i,e]:ents) if(e.o==me&&e.k==K_Outpost) op=i; printf("far outpost %u at %.1f,%.1f prog=%d\n",op,ents[op].x,ents[op].y,ents[op].prog);
  moveU({s1},ents[hb].x,ents[hb].y+1.5f); pump(7000);
  { Writer hh;hh.put<uint8_t>(C_SetHaul).put<uint32_t>(op).put<uint8_t>(10);snd(hh); }
  for(int t=0;t<26;t++){ pump(5000); printf("t=%3ds outpost stock=%d | ",t*5+5,ents.count(op)?ents[op].stock:-1); for(auto&[i,e]:ents) if(e.o==me&&e.k==K_Pawn&&(e.st&0x80)) printf("HAULER %u at %.0f,%.0f ",i,e.x,e.y); for(auto id:ships()){auto&e=ents[id];printf("SHIP at %.0f,%.0f troops=%d res=%d ",e.x,e.y,e.prog,e.stock);} printf("wood=%.0f gold=%.0f meat=%.0f\n",res[0],res[1],res[3]); }
  enet_peer_disconnect(peer,0); pump(300); return 0; }
 if(mode==7){ place(K_Harbour,te.x-6,te.y,0,6); pump(28000); uint32_t hb=firstOf(K_Harbour); train(hb,K_Ship); pump(1000); auto sh=ships(); if(sh.empty()){puts("FAIL no ship");return 1;} board(troops(),sh[0]); pump(14000); shipLine("before restart"); printf("aboard=%d\n",aboardCount()); enet_peer_disconnect(peer,0); pump(300); return 0; }
 if(mode==6){ shipLine("after restart"); printf("aboard=%d\n",aboardCount()); pump(1500); auto sh=ships(); if(!sh.empty()){ Writer d; d.put<uint8_t>(C_Disembark).put<uint32_t>(sh[0]); snd(d); pump(1500); printf("after disembark aboard=%d\n",aboardCount()); } enet_peer_disconnect(peer,0); pump(300); return 0; }
 if(mode>=3&&mode<=5){
  if(mode==3){ Writer c; c.put<uint8_t>(C_ClanCreate); c.str("Pathers"); snd(c); pump(1000); }
  if(mode==4){ pump(2000); Writer j; j.put<uint8_t>(C_ClanJoin).put<uint8_t>(1); snd(j); pump(1000); }
  for(int round=0;round<14;round++){
   if(mode==3&&round==1){ auto tr=troops(); moveU(tr,te.x+30,te.y+16); }
   if(mode==3&&round==8){ auto tr=troops(); moveU(tr,te.x-6,te.y-12); }
   pump(2000);
   int own=0,other=0,unknown=0,pts=0; for(auto&[id,v]:paths){ auto it=ents.find(id); if(it==ents.end())unknown++; else if(it->second.o==me)own++; else other++; pts+=v.size(); }
   printf("[%s round %d] path lines: own=%d allied=%d unknownEntity=%d points=%d updatesReceived=%d\n",name.c_str(),round,own,other,unknown,pts,pathUpd);
  }
  enet_peer_disconnect(peer,0); pump(300); return 0; }
 return 0; }
