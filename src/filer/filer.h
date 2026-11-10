//
// Created by cpasjuste on 12/04/18.
//

#ifndef NXFILER_FILER_H
#define NXFILER_FILER_H

#include <functional>
#include <map>
#include "cross2d/c2d.h"
#include "pplay_config.h"
#include "filer_item.h"
#include "media_file.h"
#include "highlight.h"
#include "breadcrumb.h"
#include "details_view.h"
#include "clip_rect.h"

class Main;

class MediaProbe;

class Filer : public c2d::Rectangle {

public:

    Filer(Main *main, const std::string &path, const c2d::FloatRect &rect);

    ~Filer() override;

    void setMediaInfo(const MediaFile &target, const MediaInfo &mediaInfo);

    virtual bool getDir(const std::string &path);

    virtual std::string getPath();

    virtual MediaFile getSelection() const;

    virtual std::vector<MediaFile> getFilesSnapshot() const;
    virtual bool selectByPath(const std::string &targetPath);

    virtual void setSelection(int index);

    int getSelectionIndex() const { return item_index; }

    virtual void clearHistory();

    std::string getError();

    void refresh() { dirty = true; }

    // the picture (cover.jpg...) of a listed directory, by a file of it ("" if none)
    std::string folderCoverOf(const std::string &filePath) const;

    // the media information cache was cleared
    void clearMediaInfo();

    // "<Empty>" entry of an empty directory
    static bool isPlaceholder(const MediaFile &file);

    bool onInput(c2d::Input::Player *players) override;

    void onUpdate() override;

    // faded in / out by a cover of the background color: the alphas of the parts kept (a
    // parent alpha tween would set them all)
    void setVisibility(c2d::Visibility visibility, bool tweenPlay = false) override;

    // file picker: files with these extensions listed too, A picks one, X cancels;
    // the previous directory comes back after
    void startPick(const std::vector<std::string> &extensions, const std::string &dir, const std::string &hint,
                   std::function<void(const MediaFile &)> onPick);

private:

    void endPick();

    // A on a picked file
    void pick();

    // a key (or a panel button) action
    void run(DetailsView::Action action);

    void resetProgress(const MediaFile &file);

    void forgetPlayback(const std::string &path);

    // the full duration kept, locally and on TorrServer
    void markWatched(const MediaFile &file);

    // the files of a directory (as listed for its panel): marked watched, or reset
    void markFolder(const MediaFile &dir, bool watched);

    // the saved copies of a file / directory deleted (asked first)
    void unsave(const MediaFile &file);

    // the last playlist used, else a name asked
    void addToPlaylist(const MediaFile &file);

    void renamePlaylist(const MediaFile &file);

    void deletePlaylist(const MediaFile &file);

    // the selected entry of the opened playlist
    void removeFromPlaylist();

    virtual void enter(int index);

    virtual void exit();

    // local or network root
    bool isTopLevel(const std::string &path);

    Main *main;
    std::string path;
    // the rows: the file above the first visible one, the visible ones, one under them
    // (scrolling smoothly)
    std::vector<FilerItem *> items;
    pplay::ClipRect *list = nullptr;
    Breadcrumb *crumbs = nullptr;
    int top = 0;
    float scrollOffset = 0;
    c2d::C2DClock scrollClock;

    void placeItems();
    std::vector<MediaFile> files;
    // the listed directories pictures (PosterLoader urls)
    std::map<std::string, std::string> folderCovers;
    Highlight *highlight;
    c2d::RectangleShape *scrollBar = nullptr;
    DetailsView *details = nullptr;
    MediaProbe *probe = nullptr;
    float listWidth = 0;
    float item_height;
    int item_max;
    int item_index = 0;
    std::vector<int> item_index_prev;
    c2d::C2DMutex *mutex;

    bool dirty = false;
    c2d::RectangleShape *cover = nullptr;
    // 1: covering (then hidden), -1: uncovering, 0: still
    int fadeDirection = 0;
    float coverAlpha = 0;
    c2d::C2DClock fadeClock;
    unsigned int playlistGeneration = 0;
    unsigned int durationsGeneration = 0;

    // the files with a kept position but no duration (no progress line): probed in turn
    void queueDurations();

    bool picking = false;
    std::vector<std::string> pickExtensions;
    std::function<void(const MediaFile &)> pickCallback;
    std::string pickPrevPath;
    std::string pickPrevSelection;
};

#endif //NXFILER_FILER_H
