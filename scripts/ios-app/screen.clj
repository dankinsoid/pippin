;; One screen through the level-1 ObjC bridge alone: no reconciler, no Swift (design §5, docs/notes/ios.md).
(ns pippin.screen
  (:require [clojure.core.async :as a]))

(def taps (atom 0))

;; UIButton's target is unretained, and so is the timer's receiver, so the only owner of either is this map.
(def live (atom {}))

(defn- mount! []
  (let [screen (.main-screen (objc-class "UIScreen"))
        window (.init-with-frame (.alloc (objc-class "UIWindow")) (.bounds screen))
        vc (.init (.alloc (objc-class "UIViewController")))
        root (.view vc)
        label (.init (.alloc (objc-class "UILabel")))
        button (.button-with-type (objc-class "UIButton") 1)
        target (objc-reify {} (["tap:" "v@:@"] [self sender] (swap! taps inc)))]
    (.set-background-color root (.system-background-color (objc-class "UIColor")))
    (.set-font label (.monospaced-system-font-of-size (objc-class "UIFont") 28.0 :weight 0.0))
    (.set-text-alignment label 1)
    (.set-text label (str "taps: " @taps))
    (.set-title button "tap me" :for-state 0)
    (.add-target button target :action "tap:" :for-control-events 64)
    (let [stack (.init-with-arranged-subviews (.alloc (objc-class "UIStackView")) (ns-array [label button]))]
      (.set-axis stack 1)
      (.set-spacing stack 16.0)
      (.set-alignment stack 3)
      (.set-translates-autoresizing-mask-into-constraints stack false)
      (.add-subview root stack)
      (.activate-constraints (objc-class "NSLayoutConstraint")
                             (ns-array [(.constraint-equal-to-anchor (.center-x-anchor stack) (.center-x-anchor root))
                                        (.constraint-equal-to-anchor (.center-y-anchor stack) (.center-y-anchor root))])))
    ;; The label is a subscriber of the atom, not a thing the tap handler writes (design §4 «Подписки»).
    (add-watch taps :label (fn [_ _ _ n] (.set-text label (str "taps: " n))))
    (.set-root-view-controller window vc)
    (.make-key-and-visible window)
    (swap! live assoc :window window :target target :label label :button button)
    (println "screen: mounted" (pr-str (.bounds window)) (.text label))))

;; A tap of our own, so a run without a hand on the simulator still proves the target/action path.
(defn- self-test! []
  (let [{:keys [button label]} @live]
    (dotimes [_ 3] (.send-actions-for-control-events button 64))
    (println "screen: after three taps, label =" (pr-str (.text label)) "atom =" @taps)
    ;; The main carrier is UIKit's own run loop, so a go-main body may touch the view tree.
    (a/go-main
      (a/<! (a/timeout 200))
      (swap! taps + 10)
      (println "screen: after go-main, label =" (pr-str (.text label)) "atom =" @taps))))

(defn- after [seconds f]
  (.scheduled-timer-with-time-interval (objc-class "NSTimer") seconds :repeats false
                                       :block (objc-block "v@?@" [_] (f))))

;; UIKit has no application before UIApplicationMain, so the screen mounts from the run loop main.c is about to run.
(swap! live assoc :mount (after 0.0 mount!) :self-test (after 0.5 self-test!))
