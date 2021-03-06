void geo()
{
  auto tpc = new LHTpc();
  tpc -> AddParameterFile("lamps2.par");
  //tpc -> AddParameterFile("lamps.par");
  tpc -> Init();

  /////////////////////////////////////////////////////////////////
  //pad plane
  auto padplane = tpc -> GetPadPlane();

  auto hist_padplane = padplane -> GetHist();
  //auto hist_padplane = padplane -> GetHist("0");
  for (auto iPad = 0; iPad < padplane -> GetNumPads(); ++iPad) {
    auto pad2 = padplane -> GetPad(iPad);
    auto bin = hist_padplane -> FindBin(pad2 -> GetI(), pad2 -> GetJ());
    //hist_padplane -> SetBinContent(bin, pad2 -> GetPadID());
    //hist_padplane -> SetBinContent(bin, 1);
    //hist_padplane -> SetBinContent(bin, pad2 -> GetSection());
    //hist_padplane -> SetBinContent(bin, pad2 -> GetLayer());
    //hist_padplane -> SetBinContent(bin, pad2 -> GetRow());
  }
  auto cvs = padplane -> GetCanvas();
  hist_padplane -> SetName("PadPlane");
  hist_padplane -> SetTitle("PadPlane");
  //hist_padplane -> SetTitle("PadPlane_Section");
  //hist_padplane -> SetTitle("PadPlane_Layer");
  //hist_padplane -> SetTitle("PadPlane_Row");
  //hist_padplane -> Draw("col");
  gStyle -> SetPalette(kBird);
  hist_padplane -> Draw("");
  //hist_padplane -> Draw("col");
  padplane -> DrawFrame();
  //cvs -> SaveAs(TString(hist_padplane->GetName())+".png");

  padplane -> PadPositionChecker();
  padplane -> PadNeighborChecker();
}
