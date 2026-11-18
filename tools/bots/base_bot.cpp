// Headless scenario bot for the base-mechanics features (campfires, queues, fire, clan defense, homeless, move/relocate).
// Usage: base_bot <uuid> <name> <mode> <port> [arg]
#include "shared.hpp"
#include <cstdio>
#include <map>
#include <set>
#include <unordered_map>
#include <thread>
#include <chrono>
struct E{uint32_t id;Kind k;int o;float x,y,hp,mhp;int st,aux,aim,prog,stock;};
std::unordered_map<int64_t,std::array<uint8_t,256>> terr; std::unordered_map<int64_t,std::array<uint8_t,32>> fog;
std::map<uint32_t,E> ents; float res[4]; int ulv[5]; int me=0; std::string rosterS;
struct RP{int id,fl,clan;float x,y;bool has;}; std::vector<RP> roster;
struct Q{int n,pct;std::vector<int> k;}; std::map<uint32_t,Q> queues;
int quota=-1,homeless=-1,moving=0,movePct=0; int alerts=0; float alertX=0,alertY=0;
void pk(const uint8_t*d,size_t n){ Reader r{d,d+n}; switch(r.get<uint8_t>()){
 case S_Welcome: me=r.get<uint8_t>(); printf("welcome id=%d seed=%u\n",me,r.get<uint32_t>());break;
 case S_State:{ for(float&v:res)v=r.get<float>(); for(int f=0;f<5;f++)r.get<int8_t>(); for(int f=0;f<5;f++)ulv[f]=r.get<uint8_t>(); int c=r.get<uint16_t>(); ents.clear(); for(int i=0;i<c;i++){E e;e.id=r.get<uint32_t>();e.k=Kind(r.get<uint8_t>());e.o=r.get<uint8_t>();e.x=r.get<float>();e.y=r.get<float>();e.hp=r.get<uint16_t>();e.mhp=r.get<uint16_t>();e.st=r.get<uint8_t>();e.aux=r.get<uint8_t>();e.aim=r.get<uint8_t>();e.prog=r.get<uint8_t>();e.stock=r.get<uint16_t>();ents[e.id]=e;} break;}
 case S_Fog:{ int nt=r.get<uint16_t>(); for(int i=0;i<nt;i++){int cx=r.get<int32_t>(),cy=r.get<int32_t>();auto b=r.bytes(256);std::array<uint8_t,256>a;memcpy(a.data(),b,256);terr[chunkKey(cx,cy)]=a;} int nf=r.get<uint16_t>(); for(int i=0;i<nf;i++){int cx=r.get<int32_t>(),cy=r.get<int32_t>();auto b=r.bytes(32);std::array<uint8_t,32>a;memcpy(a.data(),b,32);fog[chunkKey(cx,cy)]=a;} break;}
 case S_Text: {std::string s=r.str(); printf("  [text] %s\n",s.c_str()); break;}
 case S_Roster:{int n=r.get<uint8_t>(); roster.clear(); for(int i=0;i<n;i++){RP p;p.id=r.get<uint8_t>();p.fl=r.get<uint8_t>();p.clan=r.get<uint8_t>();r.str();p.has=r.get<uint8_t>();p.x=r.get<float>();p.y=r.get<float>();roster.push_back(p);} break;}
 case S_Queue:{int n=r.get<uint8_t>(); queues.clear(); for(int i=0;i<n;i++){uint32_t u=r.get<uint32_t>();Q q;q.n=r.get<uint8_t>();q.pct=r.get<uint8_t>();for(int j=0;j<q.n;j++)q.k.push_back(r.get<uint8_t>());queues[u]=q;} break;}
 case S_Alert:{int t=r.get<uint8_t>();float x=r.get<float>(),y=r.get<float>();std::string s=r.str();alerts++;alertX=x;alertY=y;printf("  [ALERT type %d at %.0f,%.0f] %s\n",t,x,y,s.c_str());break;}
 case S_BaseInfo:{int q=r.get<uint8_t>(),h=r.get<uint8_t>();moving=r.get<uint8_t>();movePct=r.get<uint8_t>(); if(q!=quota||h!=homeless)printf("  [baseinfo] quota=%d homeless=%d\n",q,h); quota=q;homeless=h;break;}
 default:break;}}
struct PW{ int biome(int x,int y){auto it=terr.find(chunkKey(chunkOf(x),chunkOf(y)));return it==terr.end()?INVALID_BIOME:it->second[localIdx(x,y)];}
 bool explored(int x,int y){auto it=fog.find(chunkKey(chunkOf(x),chunkOf(y)));if(it==fog.end())return false;int i=localIdx(x,y);return it->second[i>>3]>>(i&7)&1;}
 bool occupied(int x,int y){for(auto&[i,e]:ents){if(!isBuilding(e.k))continue;int tx=std::lround(e.x-KINFO[e.k].fw*.5f),ty=std::lround(e.y-KINFO[e.k].fh*.5f);if(x>=tx&&x<tx+KINFO[e.k].fw&&y>=ty&&y<ty+KINFO[e.k].fh)return true;}return false;}
 template<class F> void anchors(F f){for(auto&[i,e]:ents)if((e.k==K_TownHall||e.k==K_Outpost)&&!(e.st&BLD_DISABLED))f(Pos{e.x,e.y},e.k,e.o==me,false);}
 bool campfireNear(Pos c,float r){for(auto&[i,e]:ents)if(e.k==K_Campfire&&std::hypot(e.x-c.x,e.y-c.y)<r)return true;return false;}
 bool inClanRadius(Pos c){for(auto&p:roster){if(p.id==me||!p.has||!p.clan)continue;if(std::max(std::abs(p.x-c.x),std::abs(p.y-c.y))<=CLAN_TOWN_RADIUS)return true;}return false;} };
ENetHost*host;ENetPeer*peer;
void pump(int ms){ auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms); while(std::chrono::steady_clock::now()<end){ENetEvent ev; while(enet_host_service(host,&ev,10)>0){ if(ev.type==ENET_EVENT_TYPE_RECEIVE){pk(ev.packet->data,ev.packet->dataLength);enet_packet_destroy(ev.packet);} } } }
void snd(const Writer&w){sendPacket(peer,w);enet_host_flush(host);}
int count(Kind k){int n=0;for(auto&[i,e]:ents)if(e.k==k&&e.o==me)n++;return n;}
uint32_t myTH(){for(auto&[i,e]:ents)if(e.k==K_TownHall&&e.o==me&&e.prog>=100&&!(e.st&BLD_MOVING))return i;for(auto&[i,e]:ents)if(e.k==K_TownHall&&e.o==me)return i;return 0;}
uint32_t first(Kind k,bool done=true){for(auto&[i,e]:ents)if(e.k==k&&e.o==me&&(!done||e.prog>=100))return i;return 0;}
bool place(Kind k,float cx,float cy,int r0,int r1,int mode=0,IV* out=nullptr){ PW w; for(int r=r0;r<=r1;r++)for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++){ if(std::max(abs(dx),abs(dy))!=r)continue; int tx=(int)cx+dx,ty=(int)cy+dy; if(checkPlace(w,k,tx,ty,mode))continue; Writer b;b.put<uint8_t>(C_Build).put<uint8_t>(k).put<int32_t>(tx).put<int32_t>(ty);snd(b);pump(400);printf("placed %s at %d,%d\n",KINFO[k].name,tx,ty);if(out)*out={tx,ty};return true;} printf("no tile for %s\n",KINFO[k].name); return false;}
void train(uint32_t b,Kind k){Writer t;t.put<uint8_t>(C_Train).put<uint32_t>(b).put<uint8_t>(k);snd(t);pump(150);}
void trainN(uint32_t b,Kind k,int n){ for(int i=0;i<n;i++){ for(int w=0;w<60&&queues.count(b)&&queues[b].n>=5;w++)pump(500); train(b,k);} pump(300);}
void clanJoin(bool create){ if(create){Writer c;c.put<uint8_t>(C_ClanCreate);c.str("Defenders");snd(c);pump(800);} else {pump(1500);Writer j;j.put<uint8_t>(C_ClanJoin).put<uint8_t>(1);snd(j);pump(800);} }
void moveAttack(std::vector<uint32_t> us,uint32_t tg){ Writer a;a.put<uint8_t>(C_Attack).put<uint8_t>(uint8_t(us.size()));for(auto u:us)a.put<uint32_t>(u);a.put<uint32_t>(tg);snd(a);}
std::vector<uint32_t> army(){std::vector<uint32_t> v;for(auto&[i,e]:ents)if(e.o==me&&(e.k==K_Warrior||e.k==K_Archer||e.k==K_Lancer))v.push_back(i);return v;}
int main(int argc,char**argv){ setvbuf(stdout,nullptr,_IOLBF,0); std::string uuid=argc>1?argv[1]:"bot-uuid-0000001"; std::string name=argc>2?argv[2]:"Bot"; int mode=argc>3?atoi(argv[3]):0; int port=argc>4?atoi(argv[4]):7802; int arg=argc>5?atoi(argv[5]):0;
 enet_initialize(); host=enet_host_create(nullptr,1,2,0,0); ENetAddress a; enet_address_set_host(&a,"127.0.0.1"); a.port=uint16_t(port); peer=enet_host_connect(host,&a,2,0);
 pump(500); Writer h; h.put<uint8_t>(C_Hello); h.str(uuid); h.str(name); h.put<uint64_t>(0); h.put<uint16_t>(0); snd(h); pump(2500);
 uint32_t th=myTH(); E te=ents[th];
 printf("TH uid=%u at %.1f,%.1f res W%.0f G%.0f S%.0f M%.0f ents=%zu\n",th,te.x,te.y,res[0],res[1],res[2],res[3],ents.size());
 if(mode==20){ // ---- solo: campfires, queue, move town, relocate
   puts("== campfire anywhere + spacing");
   IV c1{0,0}; { PW w; bool ok=false; for(int r=20;r<=50&&!ok;r++)for(int dy=-r;dy<=r&&!ok;dy+=3)for(int dx=-r;dx<=r&&!ok;dx+=3){ if(std::max(abs(dx),abs(dy))!=r)continue; if(!checkPlace(w,K_Campfire,(int)te.x+dx,(int)te.y+dy)){c1={(int)te.x+dx,(int)te.y+dy};ok=true;} } printf("far campfire tile %d,%d (TH %.0f,%.0f) client-valid=%d\n",c1.x,c1.y,te.x,te.y,ok); }
   { Writer b;b.put<uint8_t>(C_Build).put<uint8_t>(K_Campfire).put<int32_t>(c1.x).put<int32_t>(c1.y);snd(b);pump(500); }
   printf("campfires now %d (expect 1)\n",count(K_Campfire));
   { PW w; printf("client check next to it: %s\n",PLACE_TEXT[checkPlace(w,K_Campfire,c1.x+2,c1.y)]); Writer b;b.put<uint8_t>(C_Build).put<uint8_t>(K_Campfire).put<int32_t>(c1.x+2).put<int32_t>(c1.y);snd(b);pump(500); }
   printf("campfires now %d (expect 1, spacing rejected)\n",count(K_Campfire));
   { PW w; int e=checkPlace(w,K_Campfire,c1.x+8,c1.y); printf("client check 8 tiles away: %s\n",PLACE_TEXT[e]); if(!e){Writer b;b.put<uint8_t>(C_Build).put<uint8_t>(K_Campfire).put<int32_t>(c1.x+8).put<int32_t>(c1.y);snd(b);pump(500);} }
   printf("campfires now %d (expect 2)\n",count(K_Campfire));
   puts("== buildings + queue");
   place(K_Outpost,te.x+16,te.y,0,10); place(K_Archery,te.x,te.y-6,0,5);
   pump(32000);
   float ox=te.x+13,oy=te.y; for(auto&[i,e]:ents) if(e.k==K_Outpost&&e.o==me){ox=e.x;oy=e.y;}
   place(K_Lumber,ox,oy,3,8); pump(500);
   uint32_t ar=first(K_Archery); printf("archery uid=%u\n",ar);
   for(int i=0;i<6;i++) train(ar,K_Warrior);
   pump(300); printf("queue: n=%d pct=%d (expect 5 queued, 6th rejected)\n",queues.count(ar)?queues[ar].n:0,queues.count(ar)?queues[ar].pct:-1);
   { Writer d;d.put<uint8_t>(C_Dequeue).put<uint32_t>(ar).put<uint8_t>(4);snd(d);pump(500); printf("after dequeue: n=%d (expect 4)\n",queues[ar].n); }
   int w0=count(K_Warrior); pump(6000); printf("warriors %d -> %d, queue n=%d pct=%d\n",w0,count(K_Warrior),queues.count(ar)?queues[ar].n:0,queues.count(ar)?queues[ar].pct:0);
   pump(25000); printf("warriors now %d (expect +4), queue %s\n",count(K_Warrior),queues.count(ar)?"nonempty":"empty");
   printf("quota=%d\n",quota); { Writer q;q.put<uint8_t>(C_SetQuota).put<uint8_t>(30);snd(q);pump(2500); printf("quota after set 30 (clamped to 20): %d\n",quota); }
   puts("== move town");
   IV nt{0,0}; { PW w; bool ok=false; for(int r=30;r<=55&&!ok;r++)for(int dy=-r;dy<=r&&!ok;dy++)for(int dx=-r;dx<=r&&!ok;dx++){ if(std::max(abs(dx),abs(dy))!=r)continue; if(!checkPlace(w,K_TownHall,(int)te.x+dx,(int)te.y+dy,PM_MoveHall)){nt={(int)te.x+dx,(int)te.y+dy};ok=true;} } printf("new TH tile %d,%d ok=%d\n",nt.x,nt.y,ok); }
   { Writer b;b.put<uint8_t>(C_Build).put<uint8_t>(K_TownHall).put<int32_t>(nt.x).put<int32_t>(nt.y);snd(b);pump(300); printf("(plain TH build must be rejected) THs=%d\n",count(K_TownHall)); }
   float g0=res[1]; { Writer b;b.put<uint8_t>(C_MoveTown).put<int32_t>(nt.x).put<int32_t>(nt.y);snd(b);pump(1500); }
   printf("THs=%d gold %.0f -> %.0f moving=%d\n",count(K_TownHall),g0,res[1],moving); for(auto&[i,e]:ents) if(e.k==K_TownHall&&e.o==me) printf("  TH %u st=%d prog=%d at %.1f,%.1f\n",i,e.st,e.prog,e.x,e.y);
   pump(9000); printf("movePct=%d moving=%d\n",movePct,moving);
   for(int i=0;i<40&&moving;i++) pump(1000);
   pump(2500);
   printf("THs=%d (expect 1)\n",count(K_TownHall)); uint32_t nth=myTH(); E n2=ents[nth]; printf("  new TH at %.1f,%.1f\n",n2.x,n2.y);
   int near2=0,tot=0; for(auto&[i,e]:ents) if(e.o==me&&(e.k==K_Warrior||e.k==K_Pawn)){tot++; if(std::hypot(e.x-n2.x,e.y-n2.y)<9)near2++;} printf("units near new TH: %d / %d\n",near2,tot);
   for(auto&[i,e]:ents) if(e.o==me&&isBuilding(e.k)) printf("  %s st=%d (disabled=%d) at %.1f,%.1f dist-to-TH=%.1f\n",KINFO[e.k].name,e.st,(e.st&BLD_DISABLED)?1:0,e.x,e.y,std::max(std::abs(e.x-n2.x),std::abs(e.y-n2.y)));
   puts("== relocate disabled archery");
   uint32_t dis=0; for(auto&[i,e]:ents) if(e.o==me&&e.k==K_Archery&&(e.st&BLD_DISABLED)) dis=i;
   if(dis){ train(dis,K_Warrior); printf("(train at disabled archery must fail) queue=%d\n",queues.count(dis)?queues[dis].n:0);
     PW w; bool done=false; for(int r=3;r<=12&&!done;r++)for(int dy=-r;dy<=r&&!done;dy++)for(int dx=-r;dx<=r&&!done;dx++){ if(std::max(abs(dx),abs(dy))!=r)continue; int tx=(int)n2.x+dx,ty=(int)n2.y+dy; if(checkPlace(w,K_Archery,tx,ty))continue; Writer b;b.put<uint8_t>(C_Relocate).put<uint32_t>(dis).put<int32_t>(tx).put<int32_t>(ty);snd(b);pump(1500);printf("relocating archery to %d,%d\n",tx,ty);done=true;}
     for(auto&[i,e]:ents) if(e.o==me&&e.k==K_Archery) printf("  archery %u st=%d prog=%d lvl=%d\n",i,e.st,e.prog,e.aux);
     for(int i=0;i<60;i++){pump(1000);int moving2=0;for(auto&[i2,e]:ents)if(e.o==me&&(e.st&BLD_MOVING))moving2++;if(!moving2)break;}
     pump(2500); for(auto&[i,e]:ents) if(e.o==me&&e.k==K_Archery) printf("  after: archery %u st=%d prog=%d lvl=%d at %.0f,%.0f\n",i,e.st,e.prog,e.aux,e.x,e.y); }
   enet_peer_disconnect(peer,0); pump(300); return 0; }
 if(mode==21){ // ---- fire attacker: archers (level 3) shoot the nearest foreign Town Hall
   place(K_Archery,te.x,te.y-6,0,5); pump(31000);
   uint32_t ar=first(K_Archery); { Writer u;u.put<uint8_t>(C_Upgrade).put<uint32_t>(ar);snd(u);pump(500); }
   printf("archer troop level: %d\n",ulv[2]);
   trainN(ar,K_Archer,arg); while(queues.count(ar)&&queues[ar].n>0) pump(500); pump(1500);
   uint32_t tg=0; float bd=1e9f; for(auto&[i,e]:ents) if(e.k==K_TownHall&&e.o!=me&&e.o<100){float d=std::hypot(e.x-te.x,e.y-te.y);if(d<bd){bd=d;tg=i;}}
   auto us=army(); printf("archers=%zu target TH %u dist %.0f\n",us.size(),tg,bd); if(tg) moveAttack(us,tg);
   for(int i=0;i<120;i++){ pump(1000); if(i%10==0){ int arr=0; for(auto&[id,e]:ents) if(e.o==me&&e.k==K_Archer&&(e.st&7)==US_Attack)arr++; printf("t=%d archers attacking=%d\n",i,arr);} }
   return 0; }
 if(mode==22){ // ---- passive victim, reports fire + extinguishing
   int maxBurn=0,maxExt=0; bool wasBurning=false; for(int i=0;i<170;i++){ pump(1000); int ext=0; for(auto&[id,e]:ents) if(e.o==me&&!isBuilding(e.k)&&(e.st&7)==US_Extinguish) ext++; bool burning=false; for(auto&[id,e]:ents) if(e.o==me&&isBuilding(e.k)&&(e.st&BLD_BURNING)) burning=true;
     if(burning!=wasBurning){printf("t=%d burning=%d (TH hp %.0f)\n",i,burning,ents.count(th)?ents[th].hp:-1.f);wasBurning=burning;} if(burning)maxBurn++; maxExt=std::max(maxExt,ext); if(ext&&i%3==0)printf("t=%d extinguishers=%d\n",i,ext); }
   printf("burning seconds=%d max extinguishers=%d\n",maxBurn,maxExt); return 0; }
 if(mode==23||mode==24){ // ---- clan member with troops (23 = creator/victim role, 24 = joiner; arg = quota for joiner)
   if(mode==23) clanJoin(true); else clanJoin(false);
   if(mode==24){ Writer q;q.put<uint8_t>(C_SetQuota).put<uint8_t>(uint8_t(arg));snd(q);pump(300);}
   place(K_Archery,te.x,te.y-6,0,5); pump(31000);
   uint32_t ar=first(K_Archery); for(int i=0;i<5;i++) train(ar,K_Warrior); pump(3000+5*900+2000);
   printf("warriors=%d quota=%d\n",count(K_Warrior),quota);
   int flagged=0; float far2=0; for(int i=0;i<150;i++){ pump(1000); int f=0; float mx=0; for(auto&[id,e]:ents) if(e.o==me&&!isBuilding(e.k)&&(e.st&UNIT_SUPPORT_FLAG)){f++; mx=std::max(mx,(float)std::hypot(e.x-te.x,e.y-te.y));}
     if(f!=flagged){printf("t=%d support-flagged units: %d (max dist from home %.0f)\n",i,f,mx);flagged=f;} far2=std::max(far2,mx); if(i%30==0)printf("t=%d flagged=%d maxdist=%.0f alerts=%d\n",i,f,mx,alerts); }
   printf("max distance travelled by patrol: %.0f alerts=%d\n",far2,alerts); return 0; }
 if(mode==25){ // ---- hostile attacker: train warriors, attack nearest foreign TH
   place(K_Archery,te.x,te.y-6,0,5); pump(31000);
   uint32_t ar=first(K_Archery); trainN(ar,K_Warrior,arg); while(queues.count(ar)&&queues[ar].n>0) pump(500); pump(1500);
   uint32_t tg=0; float bd=1e9f; for(auto&[i,e]:ents) if(e.k==K_TownHall&&e.o!=me&&e.o<100){float d=std::hypot(e.x-te.x,e.y-te.y);if(d<bd){bd=d;tg=i;}}
   auto us=army(); printf("army=%zu target %u dist %.0f\n",us.size(),tg,bd); if(tg) moveAttack(us,tg);
   pump(100000); return 0; }
 if(mode==26){ // ---- homeless victim (clan creator): wait for destruction, then rebuild near the clan hall
   clanJoin(true); { Writer q;q.put<uint8_t>(C_SetQuota).put<uint8_t>(0);snd(q);pump(300);}
   bool placed=false; for(int i=0;i<200;i++){ pump(1000); if(homeless==1&&!placed){ pump(3000); printf("HOMELESS: res W%.0f G%.0f S%.0f M%.0f, THs=%d, ents=%zu\n",res[0],res[1],res[2],res[3],count(K_TownHall),ents.size());
        Pos c{0,0}; for(auto&p:roster) if(p.id!=me&&p.has&&p.clan) c={p.x,p.y}; printf("clan hall at %.0f,%.0f\n",c.x,c.y);
        PW w; bool ok=false; int err=-1; for(int r=30;r<=100&&!ok;r+=2)for(int dy=-r;dy<=r&&!ok;dy+=2)for(int dx=-r;dx<=r&&!ok;dx+=2){ if(std::max(abs(dx),abs(dy))!=r)continue; int tx=(int)c.x+dx,ty=(int)c.y+dy; err=checkPlace(w,K_TownHall,tx,ty,PM_HomelessHall); if(err)continue;
            { int fe=checkPlace(w,K_TownHall,(int)c.x+200,(int)c.y,PM_HomelessHall); printf("far tile check: %s\n",PLACE_TEXT[fe]); }
            Writer b;b.put<uint8_t>(C_Build).put<uint8_t>(K_TownHall).put<int32_t>(tx).put<int32_t>(ty);snd(b);pump(1500);printf("placed new TH at %d,%d (free: res W%.0f G%.0f S%.0f)\n",tx,ty,res[0],res[1],res[2]);ok=true;placed=true;}
        if(!ok) printf("no valid tile (last err %d)\n",err); }
      if(placed&&i%10==0) printf("t=%d THs=%d homeless=%d\n",i,count(K_TownHall),homeless); }
   return 0; }
 if(mode==27){ // ---- clan mate that sends a support patrol to the other clan hall
   clanJoin(false); place(K_Archery,te.x,te.y-6,0,5); pump(31000);
   uint32_t ar=first(K_Archery); for(int i=0;i<6;i++) train(ar,K_Warrior); pump(7000);
   printf("warriors=%d\n",count(K_Warrior));
   Pos c{0,0}; for(auto&p:roster) if(p.id!=me&&p.has&&p.clan) c={p.x,p.y};
   { Writer s;s.put<uint8_t>(C_Support).put<float>(c.x).put<float>(c.y).put<uint8_t>(4);snd(s);pump(1500);}
   int fl=0; for(auto&[id,e]:ents) if(e.o==me&&(e.st&UNIT_SUPPORT_FLAG))fl++; printf("support flagged after command: %d (target %.0f,%.0f)\n",fl,c.x,c.y);
   for(int i=0;i<100;i++){ pump(1000); int f=0; float mn=1e9f; for(auto&[id,e]:ents) if(e.o==me&&(e.st&UNIT_SUPPORT_FLAG)){f++;mn=std::min(mn,(float)std::hypot(e.x-c.x,e.y-c.y));} if(i%10==0)printf("t=%d flagged=%d nearest to target=%.0f\n",i,f,f?mn:-1.f); }
   return 0; }
 if(mode==28){ // ---- persistence phase 1: archery + queue + quota, then leave (server is restarted by the test)
   place(K_Archery,te.x,te.y-6,0,5); pump(31000); uint32_t ar=first(K_Archery);
   { Writer q;q.put<uint8_t>(C_SetQuota).put<uint8_t>(9);snd(q);pump(300);}
   trainN(ar,K_Warrior,4); pump(1500); printf("queue before restart: n=%d pct=%d, archery uid=%u\n",queues[ar].n,queues[ar].pct,ar);
   enet_peer_disconnect(peer,0); pump(300); return 0; }
 if(mode==29){ // ---- persistence phase 2: after restart the queue, quota and units must still be there
   pump(1500); uint32_t ar=first(K_Archery); printf("after restart: archery uid=%u quota=%d queue n=%d pct=%d warriors=%d\n",ar,quota,queues.count(ar)?queues[ar].n:0,queues.count(ar)?queues[ar].pct:0,count(K_Warrior));
   pump(30000); printf("30 s later: queue n=%d warriors=%d\n",queues.count(ar)?queues[ar].n:0,count(K_Warrior)); enet_peer_disconnect(peer,0); pump(300); return 0; }
 enet_peer_disconnect(peer,0); pump(300);
}
