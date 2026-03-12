library converter_pas;

{$mode objfpc}{$H+}

uses
  converter_core;

exports
  converter_create name 'converter_create',
  converter_destroy name 'converter_destroy',
  converter_set_callbacks name 'converter_set_callbacks',
  converter_set_options name 'converter_set_options',
  converter_process_files name 'converter_process_files',
  converter_stop name 'converter_stop',
  converter_error_string name 'converter_error_string';

begin
end.
