// Synthetic low+Cross5 cost at configured low .71 versus neutral 1; not the game or whole NR.
#include "pch.h"
#include "interpass_timing_fixture.h"

int main(int argc, char** argv) {
 try {
  if(argc!=3)throw std::runtime_error("Usage: interpass_neutral_low_timing shader-dir output.csv");
  Runner r(true,32);
  auto standard=r.pipeline(argv[1],"DlssNr");
  auto cross=r.pipeline(argv[1],"dlssnr_interpass_cross5");
  auto low=r.pipeline(argv[1],"dlssnr_interpass_low36_cached");
  std::ofstream out(argv[2]);if(!out)throw std::runtime_error("Cannot create CSV");
  out<<"scale,window,sample,low_gain,gpu_ms\n"<<std::setprecision(9);
  for(int scale : {50,59,65}) {
   LowFixture fixture(r,scale,true);
   for(unsigned window=0;window<4;++window) {
    const float gain=window==0 || window==3?.71f:1.0f;
    DlssNrConstants* cb;check(fixture.crossCb->Map(0,nullptr,reinterpret_cast<void**>(&cb)));
    cb->MvScaleY=gain;fixture.crossCb->Unmap(0,nullptr);
    for(unsigned warm=0;warm<90;warm+=16)
     fixture.batch(DlssNr::InterPass::Path::FastCombinedCross5,standard.Get(),nullptr,std::min(16u,90-warm),cross.Get(),cross.Get(),low.Get());
    for(unsigned sample=0;sample<160;sample+=16) {
     auto times=fixture.batch(DlssNr::InterPass::Path::FastCombinedCross5,standard.Get(),nullptr,16,cross.Get(),cross.Get(),low.Get());
     for(unsigned i=0;i<times.size();++i)out<<scale<<','<<window<<','<<sample+i<<','<<gain<<','<<times[i]<<'\n';
    }
   }
   out.flush();if(!out)throw std::runtime_error("CSV write failed");
   std::cout<<"Neutral-low ABBA P"<<scale<<" complete\n"<<std::flush;
  }
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
}
