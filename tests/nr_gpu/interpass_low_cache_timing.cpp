// Resident Fast low-filter ABBA; timestamps include low + Cross5 and barriers, not NGX or the game.
#include "pch.h"
#include "interpass_timing_fixture.h"

int main(int argc, char** argv) {
 try {
  if (argc!=4) throw std::runtime_error("baseline-dir candidate-dir output.csv");
  Runner r(true,32);
  auto a=r.pipeline(argv[1],"dlssnr_interpass_cross5");
  auto standard=r.pipeline(argv[1],"DlssNr");
  auto low=r.pipeline(argv[1],"dlssnr_interpass_low36");
  auto lowCandidate=r.pipeline(argv[2],"dlssnr_interpass_low36_cached");
  std::ofstream out(argv[3]);if(!out)throw std::runtime_error("Cannot create CSV");
  out<<"scale,shaping,variant,window,sample,gpu_ms\n"<<std::setprecision(9);
  for (bool shape : {true}) for (int scale : {50,59,65}) {
   LowFixture fixture(r,scale,shape);
   for (unsigned window=0;window<4;++window) {
    auto* pso=a.Get();
    auto* lowPso=window==0 || window==3?low.Get():lowCandidate.Get();
    for(unsigned warm=0;warm<90;warm+=16)
     fixture.batch(DlssNr::InterPass::Path::FastCombinedCross5,standard.Get(),nullptr,std::min(16u,90-warm),a.Get(),pso,lowPso);
    for(unsigned sample=0;sample<160;sample+=16) {
     auto times=fixture.batch(DlssNr::InterPass::Path::FastCombinedCross5,standard.Get(),nullptr,16,a.Get(),pso,lowPso);
     for(unsigned i=0;i<times.size();++i)
      out<<scale<<','<<shape<<','<<(window==0 || window==3?"Fast baseline":"Fast cached Low36")<<','<<window<<','<<sample+i<<','<<times[i]<<'\n';
    }
    out.flush();if(!out)throw std::runtime_error("CSV write failed");
   }
   std::cout<<"ABBA scale="<<scale<<" shaping="<<shape<<" complete\n"<<std::flush;
  }
 } catch(const std::exception& e) { std::cerr<<e.what()<<"\n";return 1; }
}
