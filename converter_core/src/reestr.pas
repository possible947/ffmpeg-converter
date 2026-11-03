unit reestr;

// Reestr module for the converter core
// Create high-level registry for platforms, codecs, encoders, and presets
// Create low-level registry for presets ffmpeg command-line options
// get TPlatformList class with all registered platforms
// recive TReestr class with all registered items

{$mode objfpc}{$H+}

interface
// uses converter_core_types;

type
  TReestrPlatform = class;
  TReestrCodec = class;
  TReestrEncoder = class;
  TReestrPreset = class;
  TReestrPlatformArray = array of TReestrPlatform;

// TReestrCodec class represents a codec in the registry

type TReestrCodec = class
    private
      ItemsCount: integer;
    public
      ID: integer;
      Name: string;
      Available: boolean;
      Current: boolean;
      EncoderList: array of TReestrEncoder;
      function GetItemsCount: integer;
      procedure AddItem(Item: TReestrEncoder);
end;

// TReestrEncoder class represents an encoder in the registry

type TReestrEncoder = class
  private
    ItemsCount: integer;
  public
    ID: integer;
    Name: string;
    Available: boolean;
    Current: boolean;
    ContainerMovAvailable: boolean; // Indicates if the container is available
    ContainerMkvAvailable: boolean; // Indicates if the container is available
    ContainerMp4Available: boolean; // Indicates if the container is available
    AudioPcmAvailable: boolean; // Indicates if the audio format is available
    AudioAacAvailable: boolean; // Indicates if the audio format is available
    AudioAc3Available: boolean; // Indicates if the audio format is available
    AudioCopyAvailable: boolean; // Indicates if the audio format is available
    MediaChaptersAvailable: boolean; // Indicates if media chapters are available
    MediaSubtitleAvailable: boolean; // Indicates if media subtitles are available
    MediaMetadataAvailable: boolean; // Indicates if media metadata is available
    FilterOnePass: boolean; // Indicates if the one-pass filter is available
    FilterTwoPass: boolean; // Indicates if the two-pass filter is available
    PresetsList: array of TReestrPreset; // Indicates the list of available presets
    function GetItemsCount: integer;
    procedure AddItem(Item: TReestrPreset);
end;

// TReestrPreset class represents a preset in the registry

type TReestrPreset = class
  ID: integer;
  Name: string;
  Available: boolean;
  Current: boolean;
end;

// TReestrPlatform class represents a platform in the registry

type TReestrPlatform = class
  private
    ItemsCount: integer;
  public
    ID: integer;
    Name: string;
    Available: boolean;
    Current: boolean;
    CodecList: array of TReestrCodec;
    function GetItemsCount: integer;
    procedure AddItem(Item: TReestrCodec);
end;

var
  // List of platforms in the registry
  ReestrPlatformsList: array of TReestrPlatform;

// Initializes the registry with the given platforms

procedure RegistryHighInitialize(P: array of TReestrPlatform);

implementation

  function TReestrCodec.GetItemsCount: integer;
  begin
    Result := ItemsCount;
  end;

  procedure TReestrCodec.AddItem(Item: TReestrEncoder);
  begin
    Inc(ItemsCount);
    SetLength(EncoderList, ItemsCount);
    EncoderList[ItemsCount - 1] := Item;
  end;

  function TReestrEncoder.GetItemsCount: integer;
  begin
    Result := ItemsCount;
  end;

  procedure TReestrEncoder.AddItem(Item: TReestrPreset);
  begin
    Inc(ItemsCount);
    SetLength(PresetsList, ItemsCount);
    PresetsList[ItemsCount - 1] := Item;
  end;

  function TReestrPlatform.GetItemsCount: integer;
  begin
    Result := ItemsCount;
  end;

  procedure TReestrPlatform.AddItem(Item: TReestrCodec);
  begin
    Inc(ItemsCount);
    SetLength(CodecList, ItemsCount);
    CodecList[ItemsCount - 1] := Item;
  end;

  procedure RegistryHighInitialize(P: array of TReestrPlatform);
    // Initialization code for the registry goes here
    var
      Platform: TReestrPlatform;
      Codec: TReestrCodec;
      Encoder: TReestrEncoder;
      // Preset: TReestrPreset;
    begin
      Platform := TReestrPlatform.Create;
      Codec := TReestrCodec.Create;
      Encoder := TReestrEncoder.Create;
      // Preset := TReestrPreset.Create;

      // platform all (ID=1) - обязательный для всех платформ набор. Включает в себя группы
      // Video
      // Software (available - true, current - true)
      // ProRes (available - true, current - true)
      // Prores_ks (available - true, current - false)
      // x264 (available - false, current - false)
      
      // Audio (available - true, current - true)
      // copy (available - true, current - false)
      // pcm (available - true, current - true)
      // aac_fdk (available - true, current - false)
      // ac3 (available - true, current - false)

      Platform.ID := 1; // platform all
      Platform.Name := 'all';
      Platform.Available := True;
      Platform.Current := True;
      Codec.ID := 1; // codec software
      Codec.Name := 'Software';
      Codec.Available := True;
      Codec.Current := True;

      // encoder ProRes
      // | Encoder | MOV | MKV | MP4 | PCM | Audio copy | AAC | AC3 | Chapters | Subtitles | Metadata | One pass | Pre-pass |
      // | ProRes | true | false | false | true | false | false | false | true | false | false | true | true |

      Encoder.ID := 1; // encoder ProRes
      Encoder.Name := 'ProRes';
      Encoder.Available := True;
      Encoder.Current := True;
      Encoder.ContainerMovAvailable := True;
      Encoder.ContainerMkvAvailable := False;
      Encoder.Mp4Available := False;
      Encoder.AudioPcmAvailable := True;
      Encoder.AudioCopyAvailable := False;
      Encoder.AudioAacAvailable := False;
      Encoder.AudioAc3Available := False;
      Encoder.MediaChaptersAvailable := True;
      Encoder.MediaSubtitleAvailable := False;
      Encoder.MediaMetadataAvailable := False;
      Encoder.FilterOnePass := True;
      Encoder.FilterTwoPass := True;
      Codec.AddItem(Encoder);
      Encoder := TReestrEncoder.Create;

      // encoder ProRes_ks
      // | Encoder | MOV | MKV | MP4 | PCM | Audio copy | AAC | AC3 | Chapters | Subtitles | Metadata | One pass | Pre-pass |
      // | ProRes_ks | true | false | false | true | false | false | false | true | false | false | true | true |
      
      Encoder.ID := 2; // encoder ProRes_ks
      Encoder.Name := 'ProRes_ks';
      Encoder.Available := True;
      Encoder.Current := False;
      Encoder.ContainerMovAvailable := True;
      Encoder.ContainerMkvAvailable := False;
      Encoder.Mp4Available := False;
      Encoder.AudioPcmAvailable := True;
      Encoder.AudioCopyAvailable := False;
      Encoder.AudioAacAvailable := False;
      Encoder.AudioAc3Available := False;
      Encoder.MediaChaptersAvailable := True;
      Encoder.MediaSubtitleAvailable := False;
      Encoder.MediaMetadataAvailable := False;
      Encoder.FilterOnePass := True;
      Encoder.FilterTwoPass := True;
      Codec.AddItem(Encoder);
      Encoder := TReestrEncoder.Create;

      // encoder x264
      // | Encoder | MOV | MKV | MP4 | PCM | Audio copy | AAC | AC3 | Chapters | Subtitles | Metadata | One pass | Pre-pass |
      // | x264 | true | false | false | true | false | false | false | true | false | false | true | true |
      
      Encoder.ID := 3; // encoder x264
      Encoder.Name := 'x264';
      Encoder.Available := False;
      Encoder.Current := False;
      Encoder.ContainerMovAvailable := True;
      Encoder.ContainerMkvAvailable := False;
      Encoder.Mp4Available := False;
      Encoder.AudioPcmAvailable := True;
      Encoder.AudioCopyAvailable := False;
      Encoder.AudioAacAvailable := False;
      Encoder.AudioAc3Available := False;
      Encoder.MediaChaptersAvailable := True;
      Encoder.MediaSubtitleAvailable := False;
      Encoder.MediaMetadataAvailable := False;
      Encoder.FilterOnePass := True;
      Encoder.FilterTwoPass := True;
      Codec.AddItem(Encoder);
      Platform.AddItem(Codec);

      // Mux (available - true, current - false)
      // copy (available - true, current - false)
      // mkv (available - true, current - true)
      // mov (available - true, current - false)
      // mp4 (available - true, current - false).

      Codec := TReestrCodec.Create;
      Codec.ID := 2; // codec Mux
      Codec.Name := 'Mux';
      Codec.Available := True;
      Codec.Current := False;
      Encoder := TReestrEncoder.Create;

      // | Encoder | MOV | MKV | MP4 | PCM | Audio copy | AAC | AC3 | Chapters | Subtitles | Metadata | One pass | Pre-pass |
      // | copy | false | true | false | true | true | true | true | true | true | true | true | false |

      Encoder.ID := 4; // encoder copy
      Encoder.Name := 'copy';
      Encoder.Available := True;
      Encoder.Current := False;
      Encoder.ContainerMovAvailable := False;
      Encoder.ContainerMkvAvailable := True;
      Encoder.Mp4Available := False;
      Encoder.AudioPcmAvailable := True;
      Encoder.AudioCopyAvailable := True;
      Encoder.AudioAacAvailable := True;
      Encoder.AudioAc3Available := True;
      Encoder.MediaChaptersAvailable := True;
      Encoder.MediaSubtitleAvailable := True;
      Encoder.MediaMetadataAvailable := True;
      Encoder.FilterOnePass := True;
      Encoder.FilterTwoPass := False;
      Codec.AddItem(Encoder);
      Encoder := TReestrEncoder.Create;

      // | Encoder | MOV | MKV | MP4 | PCM | Audio copy | AAC | AC3 | Chapters | Subtitles | Metadata | One pass | Pre-pass |
      // | mkv | false | true | false | true | true | true | true | true | true | true | true | true |

      Encoder.ID := 5; // encoder mkv
      Encoder.Name := 'mkv';
      Encoder.Available := True;
      Encoder.Current := True;
      Encoder.ContainerMovAvailable := False;
      Encoder.ContainerMkvAvailable := True;
      Encoder.Mp4Available := False;
      Encoder.AudioPcmAvailable := True;
      Encoder.AudioCopyAvailable := True;
      Encoder.AudioAacAvailable := True;
      Encoder.AudioAc3Available := True;
      Encoder.MediaChaptersAvailable := True;
      Encoder.MediaSubtitleAvailable := True;
      Encoder.MediaMetadataAvailable := True;
      Encoder.FilterOnePass := True;
      Encoder.FilterTwoPass := True;
      Codec.AddItem(Encoder);
      Encoder := TReestrEncoder.Create;

      // | Encoder | MOV | MKV | MP4 | PCM | Audio copy | AAC | AC3 | Chapters | Subtitles | Metadata | One pass | Pre-pass |
      // | mov | true | false | false | true | false | false | false | true | false | false | true | true |

      Encoder.ID := 6; // encoder mov
      Encoder.Name := 'mov';
      Encoder.Available := True;
      Encoder.Current := False;
      Encoder.ContainerMovAvailable := True;
      Encoder.ContainerMkvAvailable := False;
      Encoder.Mp4Available := False;
      Encoder.AudioPcmAvailable := True;
      Encoder.AudioCopyAvailable := False;
      Encoder.AudioAacAvailable := False;
      Encoder.AudioAc3Available := False;
      Encoder.MediaChaptersAvailable := True;
      Encoder.MediaSubtitleAvailable := False;
      Encoder.MediaMetadataAvailable := False;
      Encoder.FilterOnePass := True;
      Encoder.FilterTwoPass := True;
      Codec.AddItem(Encoder);
      Encoder := TReestrEncoder.Create;

      // | Encoder | MOV | MKV | MP4 | PCM | Audio copy | AAC | AC3 | Chapters | Subtitles | Metadata | One pass | Pre-pass |
      // | mp4 | false | false | true | true | false | true | true | true | true | true | true | false |

      Encoder.ID := 7; // encoder mp4
      Encoder.Name := 'mp4';
      Encoder.Available := True;
      Encoder.Current := False;
      Encoder.ContainerMovAvailable := False;
      Encoder.ContainerMkvAvailable := False;
      Encoder.Mp4Available := True;
      Encoder.AudioPcmAvailable := True;
      Encoder.AudioCopyAvailable := False;
      Encoder.AudioAacAvailable := True;
      Encoder.AudioAc3Available := True;
      Encoder.MediaChaptersAvailable := True;
      Encoder.MediaSubtitleAvailable := True;
      Encoder.MediaMetadataAvailable := True;
      Encoder.FilterOnePass := True;
      Encoder.FilterTwoPass := False;
      Codec.AddItem(Encoder);
      Platform.AddItem(Codec);

      // Audio (available - true, current - true)
      // copy (available - true, current - false)
      // pcm (available - true, current - true)
      // aac_fdk (available - true, current - false)
      // ac3 (available - true, current - false)
      
      Codec := TReestrCodec.Create;
      Codec.ID := 3; // codec Audio
      Codec.Name := 'Audio';
      Codec.Available := True;
      Codec.Current := True;
      Encoder := TReestrEncoder.Create;


      Encoder.ID := 8; // encoder copy
      Encoder.Name := 'copy';
      Encoder.Available := True;
      Encoder.Current := False;
      Codec.AddItem(Encoder);
      Encoder := TReestrEncoder.Create;
      Encoder.ID := 9; // encoder pcm
      Encoder.Name := 'pcm';
      Encoder.Available := True;
      Encoder.Current := True;
      Codec.AddItem(Encoder);
      Encoder := TReestrEncoder.Create;
      Encoder.ID := 10; // encoder AAC_FDK
      Encoder.Name := 'aac_fdk';
      Encoder.Available := True;
      Encoder.Current := False;
      Codec.AddItem(Encoder);
      Encoder := TReestrEncoder.Create;
      Encoder.ID := 11; // encoder AC3
      Encoder.Name := 'ac3';
      Encoder.Available := True;
      Encoder.Current := False;
      Codec.AddItem(Encoder);
      Platform.AddItem(Codec);
      SetLength(P[0], Length(P[0]) + 1);
      P[0][High(P[0])] := Platform;
  end;
end.