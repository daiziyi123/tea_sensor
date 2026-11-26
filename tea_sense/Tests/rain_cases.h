static void Test_RainConversionAndWindow(void)
{
  SensorRainData value;
  unsigned i;uint32_t raw;
  static const uint64_t sums[3]={14379U,1572317U,3130255U};
  for(i=0U;i<3U;++i) {
    value=SensorRain_Convert(sums[i],100U);
    assert(value.valid && value.range==SENSOR_RAIN_RANGE_OK && value.rain_intensity_mm_min_x100==i*400U);
  }
  value=SensorRain_Convert(0U,1U);assert(value.valid && value.rain_intensity_mm_min_x100==0U && value.range==SENSOR_RAIN_RANGE_LOW);
  value=SensorRain_Convert(65535U,1U);assert(value.valid && value.rain_intensity_mm_min_x100==800U && value.range==SENSOR_RAIN_RANGE_HIGH);
  assert(!SensorRain_Convert(0U,0U).valid && !SensorRain_Convert(65536U,1U).valid && !SensorRain_Convert(UINT64_MAX,UINT32_MAX).valid);
  assert(SensorRain_Convert(10072U,3U).rain_intensity_mm_min_x100==83U); /* rounded raw mean would give82 */
  value=SensorRain_Convert(14378U,100U);
  assert(value.valid && !value.rain_intensity_mm_min_x100 && value.range==SENSOR_RAIN_RANGE_LOW);
  value=SensorRain_Convert(3130256U,100U);
  assert(value.valid && value.rain_intensity_mm_min_x100==800U && value.range==SENSOR_RAIN_RANGE_HIGH);
  /* Independent wide oracle; production uses only signed64, including UINT32_MAX count. */
  for(raw=0U;raw<=65535U;++raw) {
    for(i=0U;i<2U;++i) {
      uint32_t count=i?UINT32_MAX:1U;uint64_t sum=(uint64_t)raw*count;
      __int128 delta=(__int128)100*sum-(__int128)14379*count,den=(__int128)3115876*count;
      uint32_t expected=delta<=0?0U:delta>=den?800U:(uint32_t)((800*delta+den/2)/den);
      value=SensorRain_Convert(sum,count);
      assert(value.valid && value.rain_intensity_mm_min_x100==expected);
    }
  }
}

