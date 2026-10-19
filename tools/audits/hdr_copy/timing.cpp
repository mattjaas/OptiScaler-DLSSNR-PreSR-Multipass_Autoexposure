#include "pch.h"
#include "fixture.h"
int main(int argc,char**argv){try{
 if(argc!=6)throw std::runtime_error("baseline candidate output.csv normal|aa pilot|full");
 if(std::string(argv[4])!="normal" && std::string(argv[4])!="aa")throw std::runtime_error("Expected normal or aa");
 if(std::string(argv[5])!="pilot" && std::string(argv[5])!="full")throw std::runtime_error("Expected pilot or full");
 const bool control=std::string(argv[4])=="aa",pilot=std::string(argv[5])=="pilot";
 Runner r(true,40);auto a=r.pipeline(argv[1],"dlssnr_superior_encode"),b=r.pipeline(argv[2],"dlssnr_superior_encode_nocopy");
 auto area=r.pipeline(argv[1],"dlssnr_superior_area"),low=r.pipeline(argv[1],"dlssnr_superior_low");
 std::vector<ComPtr<ID3D12PipelineState>> before,after;
 for(auto n:{"normal","hdr2","hdr4"}){before.push_back(r.pipeline(argv[1],std::string("dlssnr_superior_")+n));after.push_back(r.pipeline(argv[2],std::string("dlssnr_superior_")+n+"_inplace"));}
 std::ofstream out(argv[3]);if(!out)throw std::runtime_error("CSV open failed");
 out<<"format,mapping,scale,variant,window,sample,gpu_ms\n"<<std::setprecision(9);
 for(bool half:{true,false})for(UINT profile=0;profile<3;++profile)for(UINT scale:{50,59,65}){
  DlssNrConstants c{};c.ReversibleMode=profile==0?0:profile==1?2:4;c.WhitePoint=2;c.PreExposure=1;c.ExposureMode=3;c.ExposureTrim=1;
  c.TransferStrength=c.ColourStrength=1;c.MvScaleX=.95f;c.MvScaleY=.784f;
  HdrCopyFixture f(r,3840,2160,UINT(std::lround(3840*scale/100.f)),UINT(std::lround(2160*scale/100.f)),half,0,c);
  for(UINT block=0;block<(pilot?2u:8u);++block){
   for(UINT warm=0;warm<4;++warm)f.batch(a.Get(),b.Get(),area.Get(),low.Get(),before[profile].Get(),after[profile].Get(),control,block%2!=0,4);
   for(UINT sample=0;sample<(pilot?20u:40u);sample+=4){
    auto values=f.batch(a.Get(),b.Get(),area.Get(),low.Get(),before[profile].Get(),after[profile].Get(),control,block%2!=0,4);
    for(UINT i=0;i<values.size();++i)for(UINT pos=0;pos<4;++pos){const bool candidate=(pos==1||pos==2)!=(block%2!=0);
     out<<(half?"fp16":"fp32")<<','<<c.ReversibleMode<<','<<scale<<','<<(candidate?"candidate":"base")<<','<<block*4+pos<<','<<sample+i<<','<<values[i][pos]<<'\n';
    }
   }out.flush();if(!out)throw std::runtime_error("CSV write failed");
  }std::cout<<"HDR copy ABBA fp16="<<half<<" mapping="<<c.ReversibleMode<<" P"<<scale<<" complete\n"<<std::flush;
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
