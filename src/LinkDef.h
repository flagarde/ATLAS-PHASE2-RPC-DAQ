#pragma once
// clang-format off
#ifdef __CLING__
  #pragma link C++ class DCT::RawData+;
  #pragma link C++ class std::vector<DCT::RawData>+;

  #pragma link C++ class DCT::DecodedRawData+;
  #pragma link C++ class std::vector<DCT::DecodedRawData>+;

  #pragma link C++ class DCT::IntermediateEvent+;
  #pragma link C++ class std::vector<DCT::IntermediateEvent>+;

  #pragma link C++ class DCT::Hit+;
  #pragma link C++ class DCT::Event+;
  #pragma link C++ class std::vector<DCT::Hit>+;
  #pragma link C++ class std::vector<DCT::Event>+;
#endif
// clang-format on
