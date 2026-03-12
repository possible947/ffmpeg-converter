program fpmake;

uses
  fpmkunit;

var
  P: TPackage;

begin
  P := AddPackage('ffmpeg_converter_pascal');
  P.Version := '0.2.0';
  P.Author := 'ffmpeg-converter contributors';
  P.Description := 'Free Pascal CLI, converter library, and test programs for ffmpeg-converter.';
end.
