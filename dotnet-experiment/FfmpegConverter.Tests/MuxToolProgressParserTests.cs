using FfmpegConverter.Core.Engine;

namespace FfmpegConverter.Tests;

public class MuxToolProgressParserTests
{
    [Theory]
    [InlineData("Progress: 27%", 27)]
    [InlineData("Progress: 100%", 100)]
    public void Mkvmerge_ParsesProgressLines(string line, float expected)
    {
        Assert.True(MuxToolProgressParser.TryParseMkvmergeProgress(line, out float percent));
        Assert.Equal(expected, percent);
    }

    [Theory]
    [InlineData("Progress: 101%")]
    [InlineData("Info: Progress: 27%")]
    [InlineData("Progress: complete")]
    [InlineData("Writing: 27%")]
    public void Mkvmerge_RejectsNonProgressLines(string line)
    {
        Assert.False(MuxToolProgressParser.TryParseMkvmergeProgress(line, out _));
    }

    [Theory]
    [InlineData("ISO File Writing: |==============      | (69/100)", 69)]
    [InlineData("ISO File Writing: |====================| (100/100)", 100)]
    public void Mp4Box_ParsesStableIsoWritingCounter(string line, float expected)
    {
        Assert.True(MuxToolProgressParser.TryParseMp4BoxProgress(line, out float percent));
        Assert.Equal(expected, percent);
    }

    [Theory]
    [InlineData("ISO File Writing: |===| (101/100)")]
    [InlineData("ISO File Writing: |===| (1/0)")]
    [InlineData("ISO File Writing: |===| (3/100) more")]
    [InlineData("Importing: 69%")]
    [InlineData("Writing file: 69%")]
    public void Mp4Box_RejectsOtherOrMalformedLines(string line)
    {
        Assert.False(MuxToolProgressParser.TryParseMp4BoxProgress(line, out _));
    }
}
