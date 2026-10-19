#include "pch.h"
#include "fixture.h"
int main(int argc,char**argv){try{
 if(argc!=3 && !(argc==4 && std::string(argv[3])=="--4k"))throw std::runtime_error("baseline candidate [--4k]");
 const bool full=argc==4;Runner r(true,40);
 auto a=r.pipeline(argv[1],"dlssnr_superior_encode"),b=r.pipeline(argv[2],"dlssnr_superior_encode_nocopy");
 auto area=r.pipeline(argv[1],"dlssnr_superior_area"),low=r.pipeline(argv[1],"dlssnr_superior_low");
 std::vector<ComPtr<ID3D12PipelineState>> before,after;
 for(auto n:{"normal","hdr2","hdr4"}){before.push_back(r.pipeline(argv[1],std::string("dlssnr_superior_")+n));after.push_back(r.pipeline(argv[2],std::string("dlssnr_superior_")+n+"_inplace"));}
 unsigned cases=0;size_t words=0;
 const std::vector<std::array<UINT,4>> geometries=full?std::vector<std::array<UINT,4>>{{3840,2160,1920,1080},{3840,2160,2266,1274},{3840,2160,2496,1404}}:
 std::vector<std::array<UINT,4>>{{3,3,1,1},{17,11,9,6},{33,17,17,9},{64,32,32,16},{79,43,47,25},{129,73,84,47},{33,17,33,9}};
 for(auto dims:geometries)
 for(bool half:{true,false})for(UINT mapping=0;mapping<5;++mapping)for(int pattern=0;pattern<5;++pattern)for(UINT scenario=0;scenario<3;++scenario){
  if(full && (pattern!=0 || scenario!=1))continue;
  DlssNrConstants c{};c.ReversibleMode=mapping;c.WhitePoint=2;c.PreExposure=scenario==0?.125f:scenario==1?1.f:8.f;
  c.ExposureMode=scenario==0?0:scenario==1?1:3;c.ExposureTrim=scenario==2?5:1;c.TransferStrength=.7f;c.ColourStrength=.9f;
  const float h[]={.8f,.75f,.95f},l[]={.661f,.787f,.784f};c.MvScaleX=h[scenario];c.MvScaleY=l[scenario];
  const UINT profile=mapping==2?1:mapping==4?2:0;
  HdrCopyFixture f(r,dims[0],dims[1],dims[2],dims[3],half,pattern,c);
  try{words+=f.quality(a.Get(),b.Get(),area.Get(),low.Get(),before[profile].Get(),after[profile].Get());}
  catch(...){std::cerr<<"case "<<dims[0]<<'x'<<dims[1]<<" fp16="<<half<<" mapping="<<mapping<<" pattern="<<pattern<<" scenario="<<scenario<<'\n';throw;}
  ++cases;
 }
 std::cout<<"HDR copy bit parity PASS "<<cases<<" cases "<<words<<" words\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
