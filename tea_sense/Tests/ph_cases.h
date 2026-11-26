static void Test_SoilPhConversion(void)
{
  static const struct
  {
    uint64_t sum;
    uint32_t count;
    int32_t input_mv;
    uint32_t soil_ph_x100;
    SensorPhRange range;
  } cases[] = {
    {5464U,100U,0,300U,SENSOR_PH_RANGE_OK},
    {3139807U,100U,5000,600U,SENSOR_PH_RANGE_OK},
    {4184588U,100U,6667,700U,SENSOR_PH_RANGE_OK},
    {6274150U,100U,10000,900U,SENSOR_PH_RANGE_OK},
    {5463U,100U,0,300U,SENSOR_PH_RANGE_LOW},
    {5465U,100U,0,300U,SENSOR_PH_RANGE_OK},
    {6274149U,100U,10000,900U,SENSOR_PH_RANGE_OK},
    {6274151U,100U,10000,900U,SENSOR_PH_RANGE_HIGH},
    {0U,1U,-9,300U,SENSOR_PH_RANGE_LOW},
    {65535U,1U,10446,900U,SENSOR_PH_RANGE_HIGH},
    {34567U,1U,5506,630U,SENSOR_PH_RANGE_OK},
    /* raw=106.879: pre-rounding raw to 106.88 incorrectly gives pH 3.01. */
    {106879U,1000U,8,300U,SENSOR_PH_RANGE_OK},
    /* raw=106.8791: converting from rounded 8mV incorrectly gives pH 3.00. */
    {1068791U,10000U,8,301U,SENSOR_PH_RANGE_OK},
    {2137581U,20000U,8,301U,SENSOR_PH_RANGE_OK}, /* Exact pH3.005 tie. */
    {56300U,30U,291,317U,SENSOR_PH_RANGE_OK},
    {65537U,3U,3476,509U,SENSOR_PH_RANGE_OK},
    {(uint64_t)UINT32_MAX*65535U,UINT32_MAX,10446,900U,SENSOR_PH_RANGE_HIGH},
    {(uint64_t)UINT32_MAX*32768U,UINT32_MAX,5219,613U,SENSOR_PH_RANGE_OK},
    {100000000U,UINT32_MAX,-9,300U,SENSOR_PH_RANGE_LOW}
  };
  SensorPhValue soil_ph;
  unsigned i, before;
  uint32_t raw, last_soil_ph_x100=300U;
  int32_t last_mv=-9;
  Test_Reset(0U); before=s_config_calls;
  assert(SENSOR_PH_RAW_LOW_X100==5464U && SENSOR_PH_RAW_HIGH_X100==6274150U);
  assert(SENSOR_PH_RAW_SPAN_X100==6268686U && SENSOR_PH_INPUT_SPAN_MV==10000);
  for(i=0U;i<sizeof(cases)/sizeof(cases[0]);++i)
  {
    soil_ph=SensorPh_Convert(cases[i].sum,cases[i].count);
    assert(soil_ph.valid && soil_ph.input_mv==cases[i].input_mv);
    assert(soil_ph.soil_ph_x100==cases[i].soil_ph_x100 && soil_ph.range==cases[i].range);
  }
  soil_ph=SensorPh_Convert(0U,0U);
  assert(!soil_ph.valid && soil_ph.range==SENSOR_PH_RANGE_UNAVAILABLE);
  assert(!SensorPh_Convert(65536U,1U).valid && !SensorPh_Convert(UINT64_MAX,UINT32_MAX).valid);
  for(raw=0U;raw<=UINT16_MAX;++raw)
  {
    soil_ph=SensorPh_Convert(raw,1U);
    assert(soil_ph.valid && soil_ph.input_mv>=last_mv && soil_ph.soil_ph_x100>=last_soil_ph_x100);
    assert(soil_ph.soil_ph_x100>=300U && soil_ph.soil_ph_x100<=900U);
    last_mv=soil_ph.input_mv; last_soil_ph_x100=soil_ph.soil_ph_x100;
  }
  assert(s_config_calls==before && !s_ad4_reads); /* Conversion never starts ADC. */
  assert(!strcmp(SensorPh_RangeName(SENSOR_PH_RANGE_LOW),"LOW"));
  assert(!strcmp(SensorPh_RangeName(SENSOR_PH_RANGE_OK),"OK"));
  assert(!strcmp(SensorPh_RangeName(SENSOR_PH_RANGE_HIGH),"HIGH"));
  assert(!strcmp(SensorPh_RangeName(SENSOR_PH_RANGE_UNAVAILABLE),"NA"));
}


