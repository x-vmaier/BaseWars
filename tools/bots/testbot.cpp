// Headless test bot: joins, builds, waits for supply-line income, optional alliance, prints observations.
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
std::map<uint32_t,E> ents; float res[4]; int rep[5]; int ulv[5]; int me=0; std::vector<std::string> texts; std::string rosterS;
void pk(const uint8_t*d,size_t n){ Reader r{d,d+n}; switch(r.get<uint8_t>()){
 case S_Welcome: me=r.get<uint8_t>(); printf("welcome id=%d seed=%u cam=%.1f,%.1f name=%s\n",me,r.get<uint32_t>(),r.get<float>(),r.get<float>(),r.str().c_str());break;
 case S_State:{ for(float&v:res)v=r.get<float>(); for(int f=0;f<5;f++)rep[f]=r.get<int8_t>(); for(int f=0;f<5;f++)ulv[f]=r.get<uint8_t>(); int c=r.get<uint16_t>(); ents.clear(); for(int i=0;i<c;i++){E e;e.id=r.get<uint32_t>();e.k=Kind(r.get<uint8_t>());e.o=r.get<uint8_t>();e.x=r.get<float>();e.y=r.get<float>();e.hp=r.get<uint16_t>();e.mhp=r.get<uint16_t>();e.st=r.get<uint8_t>();e.aux=r.get<uint8_t>();e.aim=r.get<uint8_t>();e.prog=r.get<uint8_t>();e.stock=r.get<uint16_t>();ents[e.id]=e;} break;}
 case S_Fog:{ int nt=r.get<uint16_t>(); for(int i=0;i<nt;i++){int cx=r.get<int32_t>(),cy=r.get<int32_t>();auto b=r.bytes(256);std::array<uint8_t,256>a;memcpy(a.data(),b,256);terr[chunkKey(cx,cy)]=a;} int nf=r.get<uint16_t>(); for(int i=0;i<nf;i++){int cx=r.get<int32_t>(),cy=r.get<int32_t>();auto b=r.bytes(32);std::array<uint8_t,32>a;memcpy(a.data(),b,32);fog[chunkKey(cx,cy)]=a;} break;}
 case S_Text: {std::string s=r.str(); printf("  [text] %s\n",s.c_str()); break;}
 case S_Roster:{int n=r.get<uint8_t>(); rosterS.clear(); for(int i=0;i<n;i++){int id=r.get<uint8_t>(),fl=r.get<uint8_t>(),cl=r.get<uint8_t>();std::string nm=r.str();int has=r.get<uint8_t>();float x=r.get<float>(),y=r.get<float>();rosterS+=nm+"("+std::to_string(id)+",f"+std::to_string(fl)+",clan"+std::to_string(cl)+(has?",loc "+std::to_string(int(x))+","+std::to_string(int(y)):"")+") ";} break;}
 case S_Chat:{int ch=r.get<uint8_t>(),sd=r.get<uint8_t>();std::string nm=r.str(),tx=r.str();printf("  [chat %d] %s: %s\n",ch,nm.c_str(),tx.c_str());break;}
 default:break;}}
struct PW{ int biome(int x,int y){auto it=terr.find(chunkKey(chunkOf(x),chunkOf(y)));return it==terr.end()?INVALID_BIOME:it->second[localIdx(x,y)];}
 bool explored(int x,int y){auto it=fog.find(chunkKey(chunkOf(x),chunkOf(y)));if(it==fog.end())return false;int i=localIdx(x,y);return it->second[i>>3]>>(i&7)&1;}
 bool occupied(int x,int y){for(auto&[i,e]:ents){if(!isBuilding(e.k))continue;int tx=std::lround(e.x-KINFO[e.k].fw*.5f),ty=std::lround(e.y-KINFO[e.k].fh*.5f);if(x>=tx&&x<tx+KINFO[e.k].fw&&y>=ty&&y<ty+KINFO[e.k].fh)return true;}return false;}
 template<class F> void anchors(F f){for(auto&[i,e]:ents)if(e.k==K_TownHall||e.k==K_Outpost)f(Pos{e.x,e.y},e.k,e.o==me,false);} };
ENetHost*host;ENetPeer*peer;
void pump(int ms){ auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms); while(std::chrono::steady_clock::now()<end){ENetEvent ev; while(enet_host_service(host,&ev,10)>0){ if(ev.type==ENET_EVENT_TYPE_RECEIVE){pk(ev.packet->data,ev.packet->dataLength);enet_packet_destroy(ev.packet);} } } }
void snd(const Writer&w){sendPacket(peer,w);enet_host_flush(host);}
int count(Kind k){int n=0;for(auto&[i,e]:ents)if(e.k==k&&e.o==me)n++;return n;}
int main(int argc,char**argv){ std::string uuid=argc>1?argv[1]:"bot-uuid-0000001"; std::string name=argc>2?argv[2]:"Bot"; int mode=argc>3?atoi(argv[3]):0;
 enet_initialize(); host=enet_host_create(nullptr,1,2,0,0); ENetAddress a; enet_address_set_host(&a,"127.0.0.1"); a.port=argc>4?uint16_t(atoi(argv[4])):7799; peer=enet_host_connect(host,&a,2,0);
 pump(500); Writer h; h.put<uint8_t>(C_Hello); h.str(uuid); h.str(name); h.put<uint64_t>(0); h.put<uint16_t>(0); snd(h); pump(1500);
 uint32_t th=0; E te{}; for(auto&[i,e]:ents) if(e.k==K_TownHall&&e.o==me){th=i;te=e;}
 printf("TH uid=%u at %.1f,%.1f res W%.0f G%.0f S%.0f M%.0f ents=%zu chunks terr=%zu fog=%zu\n",th,te.x,te.y,res[0],res[1],res[2],res[3],ents.size(),terr.size(),fog.size());
 if(mode==1){ return 0; }
 if(mode==9){ for(int i=0;i<170;i++){ pump(1000); if(i%30==0){int e=0;for(auto&[id,en]:ents) if(en.o>=100)e++; printf("t=%d enemy faction entities visible: %d, TH hp %.0f, res W%.0f G%.0f S%.0f M%.0f\\n",i,e,ents.count(th)?ents[th].hp:-1.f,res[0],res[1],res[2],res[3]);} } return 0; }
 if(mode==7||mode==8){
   auto place=[&](Kind k,float cx,float cy,int r0,int r1){ PW w; for(int r=r0;r<=r1;r++)for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++){ if(std::max(abs(dx),abs(dy))!=r)continue; int tx=(int)cx+dx,ty=(int)cy+dy; if(checkPlace(w,k,tx,ty))continue; Writer b;b.put<uint8_t>(C_Build).put<uint8_t>(k).put<int32_t>(tx).put<int32_t>(ty);snd(b);pump(400);printf("placed %s at %d,%d\n",KINFO[k].name,tx,ty);return true;} printf("no tile for %s\n",KINFO[k].name); return false;};
   { Writer b;b.put<uint8_t>(C_Build).put<uint8_t>(K_TownHall).put<int32_t>(20).put<int32_t>(20);snd(b);pump(300); }   // must be rejected: one Town Hall only
   place(K_Outpost,te.x+13,te.y,0,6); place(K_Archery,te.x,te.y-6,0,5); place(K_Storage,te.x+4,te.y+5,0,5);
   pump(32000);   // buildings finish (outpost 20s, archery 30s)

   float ox=te.x+13,oy=te.y; for(auto&[i,e]:ents) if(e.k==K_Outpost&&e.o==me){ox=e.x;oy=e.y;}
   place(K_Lumber,ox,oy,3,8); place(K_Mine,ox,oy,3,9);
   uint32_t op=0,ar=0,st=0; for(auto&[i,e]:ents) if(e.o==me){ if(e.k==K_Outpost)op=i; if(e.k==K_Archery)ar=i; if(e.k==K_Storage)st=i; }
   printf("outpost uid=%u archery uid=%u storage uid=%u\n",op,ar,st);
   { Writer h;h.put<uint8_t>(C_SetHaul).put<uint32_t>(op).put<uint8_t>(10);snd(h); pump(200); }
   // training rules
   auto train=[&](Kind k){ Writer t;t.put<uint8_t>(C_Train).put<uint32_t>(ar).put<uint8_t>(k);snd(t);pump(500); };
   train(K_Warrior); train(K_Lancer); train(K_Archer);
   { Writer t;t.put<uint8_t>(C_Train).put<uint32_t>(th).put<uint8_t>(K_Warrior);snd(t);pump(400); printf("(trained at Town Hall: must be ignored)\n"); }
   { Writer u;u.put<uint8_t>(C_UpgradeUnit).put<uint8_t>(K_Warrior);snd(u);pump(500); printf("warrior level now %d\n",ulv[1]); }
   { Writer c;c.put<uint8_t>(C_Chat).put<uint8_t>(0);c.str("hello world");snd(c);pump(400); }
   pump(mode==8?16000:100000);
   if(mode==8){enet_peer_disconnect(peer,0);pump(300);return 0;}
   for(auto&[i,e]:ents) if(e.o==me&&isBuilding(e.k)) printf("  %s L%d prog=%d stock=%d haul%%=%d hp=%.0f\n",KINFO[e.k].name,e.aux,e.prog,e.stock,e.aim,e.hp);
   int hauls=0; for(auto&[i,e]:ents) if(e.o==me&&(e.st&0x80)) hauls++; printf("hauling pawns now: %d, res W%.0f G%.0f S%.0f M%.0f\n",hauls,res[0],res[1],res[2],res[3]);
   enet_peer_disconnect(peer,0); pump(300); return 0; }
 if(mode==6){ Writer j; j.put<uint8_t>(C_ClanJoin).put<uint8_t>(1); snd(j); pump(1000);
   Writer t; t.put<uint8_t>(C_Transfer).put<uint8_t>(1); t.put<float>(80.f).put<float>(0.f).put<float>(0.f).put<float>(0.f); snd(t); pump(100000); return 0; }
 if(mode==4||mode==5){ // social test: 4 = founder, 5 = joiner
   if(mode==4){ Writer c; c.put<uint8_t>(C_ClanCreate); c.str("Testers"); snd(c); pump(800); }
   else { pump(1500); Writer j; j.put<uint8_t>(C_ClanJoin).put<uint8_t>(1); snd(j); pump(800); }
   printf("roster: %s\n",rosterS.c_str());
   // donate to faction 0 twice, check rep
   { Writer d; d.put<uint8_t>(C_Donate).put<uint8_t>(0).put<uint8_t>(1).put<float>(100.f); snd(d); pump(600); printf("rep after donating 100 gold: %d (gold now %.0f)\n",rep[0],res[1]); }
   if(mode==4){ pump(3000); printf("roster: %s\n",rosterS.c_str()); // send convoy to member 2
     Writer t; t.put<uint8_t>(C_Transfer).put<uint8_t>(2); t.put<float>(50.f).put<float>(0.f).put<float>(0.f).put<float>(0.f); snd(t); pump(1500);
     int cv=0; for(auto&[i,e]:ents) if(e.o==me&&(e.st&0x80)) cv++; printf("convoys visible: %d wood now %.0f\n",cv,res[0]);
     pump(25000); cv=0; for(auto&[i,e]:ents) if(e.o==me&&(e.st&0x80)) {cv++; printf("convoy at %.1f,%.1f\n",e.x,e.y);} printf("convoys after 25s: %d\n",cv);
   } else { float w0=res[0]; pump(30000); printf("joiner wood %.0f -> %.0f\n",w0,res[0]); }
   enet_peer_disconnect(peer,0); pump(300); return 0; }
 // try placing each building around TH using shared checkPlace, count successes
 Kind order[]={K_Lumber,K_Mine,K_Campfire,K_Storage,K_Outpost,K_Harbour};
 for(Kind k:order){ PW w; bool placed=false; int errs[P_Count]={};
   for(int r=3;r<=14&&!placed;r++)for(int dy=-r;dy<=r&&!placed;dy++)for(int dx=-r;dx<=r&&!placed;dx++){ if(std::max(abs(dx),abs(dy))!=r)continue; int tx=(int)te.x+dx,ty=(int)te.y+dy; int e=checkPlace(w,k,tx,ty); errs[e]++; if(e)continue;
      Writer b; b.put<uint8_t>(C_Build).put<uint8_t>(k).put<int32_t>(tx).put<int32_t>(ty); snd(b); pump(300); placed=true; printf("placed %s at %d,%d (client-valid)\n",KINFO[k].name,tx,ty);}
   if(!placed){printf("no valid tile for %s; errs:",KINFO[k].name);for(int i=0;i<P_Count;i++)printf(" %s=%d",PLACE_TEXT[i],errs[i]);puts("");}
   pump(200);
 }
 printf("buildings now: lumber=%d mine=%d camp=%d barracks=%d outpost=%d harbour=%d\n",count(K_Lumber),count(K_Mine),count(K_Campfire),count(K_Storage),count(K_Outpost),count(K_Harbour));
 // invalid placement must be rejected server-side: build on top of TH
 {Writer b;b.put<uint8_t>(C_Build).put<uint8_t>(K_Lumber).put<int32_t>((int)te.x).put<int32_t>((int)te.y);snd(b);pump(300);}
 float r0[4];memcpy(r0,res,16); 
 pump(60000);
 int pawns=count(K_Pawn); printf("after 60s: pawns=%d res W%.0f G%.0f S%.0f M%.0f (start W%.0f G%.0f S%.0f M%.0f)\n",pawns,res[0],res[1],res[2],res[3],r0[0],r0[1],r0[2],r0[3]);
 for(auto&[i,e]:ents) if(e.o==me&&isBuilding(e.k)) printf("  %s prog=%d stock=%d hp=%.0f\n",KINFO[e.k].name,e.prog,e.stock,e.hp);
 for(auto&[i,e]:ents) if(e.o==me&&e.k==K_Pawn) printf("  pawn st=%d aux=%d at %.1f,%.1f\n",e.st,e.aux,e.x,e.y);
 // train
 {Writer t;t.put<uint8_t>(C_Train).put<uint32_t>(th).put<uint8_t>(K_Monk);snd(t);pump(500); printf("monks=%d\n",count(K_Monk));}
 if(mode==2){ puts("holding 20s"); pump(20000);} 
 enet_peer_disconnect(peer,0); pump(300);
}
