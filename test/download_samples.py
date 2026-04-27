from argparse import ArgumentParser
from pathlib import Path
from urllib.parse import urljoin
from urllib.request import urlretrieve


def download(url: str, file: Path) -> None:
    if file.is_file():
        print(f"[skip] {file.name}")
        return

    print(f"[download] {url}")
    urlretrieve(url, file)


def main() -> None:
    parser = ArgumentParser(
        description="Sample downloader."
    )
    parser.add_argument(
        "sample_path",
        type=Path,
        help="""
        Path where to save the samples.
    """,
    )

    args = parser.parse_args()

    sample_path: Path = args.sample_path

    SAMPLES_URL = "https://storage.googleapis.com/ffms2tests/"
    SAMPLES_FILE = [
        "test.mp4",
        "hdr10tags-both.mkv",
        "hdr10tags-container.mkv",
        "hdr10tags-stream.mp4",
        "qrvideo_hflip_90.mov",
        "qrvideo_hflip_270.mov",
        "qrvideo_vflip.mov",
        "vp9_audfirst.webm",
        "qrvideo_24fps_1elist_1ctts.mov",
        "qrvideo_24fps_1elist_ends_last_bframe.mov",
        "qrvideo_24fps_1elist_noctts.mov",
        "qrvideo_24fps_2elist_elist1_dur_zero.mov",
        "qrvideo_24fps_2elist_elist1_ends_bframe.mov",
        "qrvideo_24fps_2s_3elist.mov",
        "qrvideo_24fps_3elist_1ctts.mov",
        "qrvideo_24fps_elist_starts_ctts_2ndsample.mov",
        "qrvideo_stream_shorter_than_movie.mov",
    ]

    if not sample_path.is_dir():
        sample_path.mkdir(parents=True)

    for sample in SAMPLES_FILE:
        url = urljoin(SAMPLES_URL, sample)
        file = sample_path.joinpath(sample)
        download(url, file)


if __name__ == "__main__":
    main()
