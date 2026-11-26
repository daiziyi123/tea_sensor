static void Test_WindDirectionConversion(void)
{
  static const struct { uint32_t raw_x100; int32_t mv; uint32_t angle; } points[] = {
    {640067U,1000,0U}, {1561418U,2498,13483U},
    {2192014U,3524,22712U}, {3100043U,5000,36000U}
  };
  SensorWindDirectionValue value, previous;
  unsigned index;
  uint32_t raw;
  Test_Reset(0U);
  for(index=0U;index<sizeof(points)/sizeof(points[0]);++index)
  {
    value=SensorWindDirection_Convert(points[index].raw_x100);
    assert(value.valid && value.input_mv==points[index].mv && value.angle_deg_x100==points[index].angle);
    assert(value.range==SENSOR_WIND_DIRECTION_RANGE_OK);
    fprintf(stderr,"AD3 fixed-point check: raw_x100=%lu,input_mv=%ld,angle_deg=%lu.%02lu,range=OK\n",
      (unsigned long)points[index].raw_x100,(long)value.input_mv,
      (unsigned long)(value.angle_deg_x100/100U),(unsigned long)(value.angle_deg_x100%100U));
  }
  assert(s_config_calls==0U && s_ad3_reads==0U); /* Conversion never starts ADC. */
  value=SensorWindDirection_Convert(640066U);
  assert(value.valid && value.input_mv==1000 && value.angle_deg_x100==0U && value.range==SENSOR_WIND_DIRECTION_RANGE_LOW);
  value=SensorWindDirection_Convert(3100044U);
  assert(value.valid && value.input_mv==5000 && value.angle_deg_x100==36000U && value.range==SENSOR_WIND_DIRECTION_RANGE_HIGH);
  value=SensorWindDirection_Convert(0U);
  assert(value.valid && value.input_mv==-41 && value.angle_deg_x100==0U && value.range==SENSOR_WIND_DIRECTION_RANGE_LOW);
  value=SensorWindDirection_Convert(6553500U);
  assert(value.valid && value.input_mv==10615 && value.angle_deg_x100==36000U && value.range==SENSOR_WIND_DIRECTION_RANGE_HIGH);
  value=SensorWindDirection_Convert(6553501U); assert(!value.valid && value.range==SENSOR_WIND_DIRECTION_RANGE_UNAVAILABLE);
  value=SensorWindDirection_Convert(UINT32_MAX); assert(!value.valid);
  /* Two-point relation is monotone over the complete physical ADC domain. */
  previous=SensorWindDirection_Convert(0U);
  for(raw=1U;raw<=UINT16_MAX;++raw)
  {
    value=SensorWindDirection_Convert(raw*100U);
    assert(value.valid && value.angle_deg_x100<=36000U);
    assert(value.input_mv>=previous.input_mv && value.angle_deg_x100>=previous.angle_deg_x100);
    assert(value.range==(raw*100U<640067U?SENSOR_WIND_DIRECTION_RANGE_LOW:
                         raw*100U>3100043U?SENSOR_WIND_DIRECTION_RANGE_HIGH:SENSOR_WIND_DIRECTION_RANGE_OK));
    previous=value;
  }
  /* 2498 rounded mV would imply 134.82 degrees; direct raw must yield 134.83. */
  value=SensorWindDirection_Convert(1561400U);
  assert(value.input_mv==2498 && value.angle_deg_x100==13483U);
  assert(!strcmp(SensorWindDirection_RangeName(SENSOR_WIND_DIRECTION_RANGE_LOW),"LOW"));
  assert(!strcmp(SensorWindDirection_RangeName(SENSOR_WIND_DIRECTION_RANGE_HIGH),"HIGH"));
  assert(!strcmp(SensorWindDirection_RangeName(SENSOR_WIND_DIRECTION_RANGE_UNAVAILABLE),"NA"));
}


