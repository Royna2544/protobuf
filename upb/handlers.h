/*
 * upb - a minimalist implementation of protocol buffers.
 *
 * Copyright (c) 2010-2012 Google Inc.  See LICENSE for details.
 * Author: Josh Haberman <jhaberman@gmail.com>
 *
 * A upb_handlers is like a virtual table for a upb_msgdef.  Each field of the
 * message can have associated functions that will be called when we are
 * parsing or visiting a stream of data.  This is similar to how handlers work
 * in SAX (the Simple API for XML).
 *
 * The handlers have no idea where the data is coming from, so a single set of
 * handlers could be used with two completely different data sources (for
 * example, a parser and a visitor over in-memory objects).  This decoupling is
 * the most important feature of upb, because it allows parsers and serializers
 * to be highly reusable.
 *
 * This is a mixed C/C++ interface that offers a full API to both languages.
 * See the top-level README for more information.
 */

#ifndef UPB_HANDLERS_H
#define UPB_HANDLERS_H

#include "upb/def.h"

#ifdef __cplusplus

namespace upb {
class HandlerAttributes;
class Handlers;
template <class T> class Handler;
template <class T> struct CanonicalType;
}  // namespace upb

typedef upb::HandlerAttributes upb_handlerattr;
typedef upb::Handlers upb_handlers;
#else
struct upb_handlerattr;
struct upb_handlers;
struct upb_sinkframe;
typedef struct upb_handlerattr upb_handlerattr;
typedef struct upb_handlers upb_handlers;
typedef struct upb_sinkframe upb_sinkframe;
#endif

// The maximum depth that the handler graph can have.  This is a resource limit
// for the C stack since we sometimes need to recursively traverse the graph.
// Cycles are ok; the traversal will stop when it detects a cycle, but we must
// hit the cycle before the maximum depth is reached.
//
// If having a single static limit is too inflexible, we can add another variant
// of Handlers::Freeze that allows specifying this as a parameter.
#define UPB_MAX_HANDLER_DEPTH 64

// All the different types of handlers that can be registered.
// Only needed for the advanced functions in upb::Handlers.
typedef enum {
  UPB_HANDLER_INT32,
  UPB_HANDLER_INT64,
  UPB_HANDLER_UINT32,
  UPB_HANDLER_UINT64,
  UPB_HANDLER_FLOAT,
  UPB_HANDLER_DOUBLE,
  UPB_HANDLER_BOOL,
  UPB_HANDLER_STARTSTR,
  UPB_HANDLER_STRING,
  UPB_HANDLER_ENDSTR,
  UPB_HANDLER_STARTSUBMSG,
  UPB_HANDLER_ENDSUBMSG,
  UPB_HANDLER_STARTSEQ,
  UPB_HANDLER_ENDSEQ,
} upb_handlertype_t;

#define UPB_HANDLER_MAX (UPB_HANDLER_ENDSEQ+1)

#define UPB_BREAK NULL

// A convenient definition for when no closure is needed.
extern char _upb_noclosure;
#define UPB_NO_CLOSURE &_upb_noclosure

// A selector refers to a specific field handler in the Handlers object
// (for example: the STARTSUBMSG handler for field "field15").
typedef int32_t upb_selector_t;
typedef void upb_func();

#ifdef __cplusplus
extern "C" {
#endif

UPB_INLINE upb_func *upb_handlers_gethandler(const upb_handlers *h,
                                             upb_selector_t s);
UPB_INLINE const void *upb_handlerattr_handlerdata(const upb_handlerattr *attr);
UPB_INLINE const void *upb_handlers_gethandlerdata(const upb_handlers *h,
                                                   upb_selector_t s);

#ifdef __cplusplus
}
#endif


// Static selectors for upb::Handlers.
#define UPB_STARTMSG_SELECTOR 0
#define UPB_ENDMSG_SELECTOR 1
#define UPB_STATIC_SELECTOR_COUNT 2

// Static selectors for upb::BytesHandler.
#define UPB_STARTSTR_SELECTOR 0
#define UPB_STRING_SELECTOR 1
#define UPB_ENDSTR_SELECTOR 2

typedef void upb_handlerfree(void *d);

#ifdef __cplusplus

class upb::HandlerAttributes {
 public:
  HandlerAttributes();
  ~HandlerAttributes();

  // Sets the handler data that will be passed as the second parameter of the
  // handler.
  //
  // Warning: if you use these attributes for multiple handlers, the
  // cleanup handler will be called once for each handler it was successfully
  // set on.
  bool SetHandlerData(void *handler_data, upb_handlerfree *cleanup);
  void *handler_data() const;

 private:
  friend UPB_INLINE const void * ::upb_handlerattr_handlerdata(
      const upb_handlerattr *attr);

#else
struct upb_handlerattr {
#endif
  void *handler_data_;
  upb_handlerfree *cleanup;
};

typedef struct {
  upb_func *func;
  upb_handlerattr attr;
} upb_handlers_tabent;

#ifdef __cplusplus

// A upb::Handlers object represents the set of handlers associated with a
// message in the graph of messages.  You can think of it as a big virtual
// table with functions corresponding to all the events that can fire while
// parsing or visiting a message of a specific type.
//
// Any handlers that are not set behave as if they had successfully consumed
// the value.  Any unset Start* handlers will propagate their closure to the
// inner frame.
//
// The easiest way to create the *Handler objects needed by the Set* methods is
// with the UpbBind() and UpbMakeHandler() macros; see below.
class upb::Handlers {
 public:
  typedef upb_selector_t Selector;
  typedef upb_handlertype_t Type;

  typedef Handler<void *(*)(void *, const void *)> StartFieldHandler;
  typedef Handler<bool (*)(void *, const void *)> EndFieldHandler;
  typedef Handler<bool (*)(void *, const void *)> StartMessageHandler;
  typedef Handler<bool (*)(void *, const void *, Status*)> EndMessageHandler;
  typedef Handler<void *(*)(void *, const void *, size_t)> StartStringHandler;
  typedef Handler<size_t(*)(void *, const void *, const char *, size_t)>
      StringHandler;

  template <class T> struct ValueHandler {
    typedef Handler<bool(*)(void *, const void *, T)> H;
  };

  typedef ValueHandler<int32_t>::H     Int32Handler;
  typedef ValueHandler<int64_t>::H     Int64Handler;
  typedef ValueHandler<uint32_t>::H    UInt32Handler;
  typedef ValueHandler<uint64_t>::H    UInt64Handler;
  typedef ValueHandler<float>::H       FloatHandler;
  typedef ValueHandler<double>::H      DoubleHandler;
  typedef ValueHandler<bool>::H        BoolHandler;

  // Any function pointer can be converted to this and converted back to its
  // correct type.
  typedef void GenericFunction();

  typedef void HandlersCallback(void *closure, upb_handlers *h);

  // Returns a new handlers object for the given frozen msgdef.
  // Returns NULL if memory allocation failed.
  static reffed_ptr<Handlers> New(const MessageDef *m);

  // Convenience function for registering a graph of handlers that mirrors the
  // graph of msgdefs for some message.  For "m" and all its children a new set
  // of handlers will be created and the given callback will be invoked,
  // allowing the client to register handlers for this message.  Note that any
  // subhandlers set by the callback will be overwritten.
  static reffed_ptr<const Handlers> NewFrozen(const MessageDef *m,
                                              HandlersCallback *callback,
                                              void *closure);

  // Functionality from upb::RefCounted.
  bool IsFrozen() const;
  void Ref(const void* owner) const;
  void Unref(const void* owner) const;
  void DonateRef(const void *from, const void *to) const;
  void CheckRef(const void *owner) const;

  // All handler registration functions return bool to indicate success or
  // failure; details about failures are stored in this status object.  If a
  // failure does occur, it must be cleared before the Handlers are frozen,
  // otherwise the freeze() operation will fail.  The functions may *only* be
  // used while the Handlers are mutable.
  const Status* status();
  void ClearError();

  // Call to freeze these Handlers.  Requires that any SubHandlers are already
  // frozen.  For cycles, you must use the static version below and freeze the
  // whole graph at once.
  bool Freeze(Status* s);

  // Freezes the given set of handlers.  You may not freeze a handler without
  // also freezing any handlers they point to.
  static bool Freeze(Handlers*const* handlers, int n, Status* s);
  static bool Freeze(const vector<Handlers*>& handlers, Status* s);

  // Returns the msgdef associated with this handlers object.
  const MessageDef* message_def() const;

  // Sets the startmsg handler for the message, which is defined as follows:
  //
  //   bool startmsg(MyType* closure) {
  //     // Called when the message begins.  Returns true if processing should
  //     // continue.
  //     return true;
  //   }
  bool SetStartMessageHandler(const StartMessageHandler& handler);

  // Sets the endmsg handler for the message, which is defined as follows:
  //
  //   bool endmsg(MyType* closure, upb_status *status) {
  //     // Called when processing of this message ends, whether in success or
  //     // failure.  "status" indicates the final status of processing, and
  //     // can also be modified in-place to update the final status.
  //   }
  bool SetEndMessageHandler(const EndMessageHandler& handler);

  // Sets the value handler for the given field, which is defined as follows
  // (this is for an int32 field; other field types will pass their native
  // C/C++ type for "val"):
  //
  //   bool OnValue(MyClosure* c, const MyHandlerData* d, int32_t val) {
  //     // Called when the field's value is encountered.  "d" contains
  //     // whatever data was bound to this field when it was registered.
  //     // Returns true if processing should continue.
  //     return true;
  //   }
  //
  //   handers->SetInt32Handler(f, UpbBind(OnValue, new MyHandlerData(...)));
  //
  // The value type must exactly match f->type().
  // For example, a handler that takes an int32_t parameter may only be used for
  // fields of type UPB_TYPE_INT32 and UPB_TYPE_ENUM.
  //
  // Returns false if the handler failed to register; in this case the cleanup
  // handler (if any) will be called immediately.
  bool SetInt32Handler (const FieldDef* f,  const Int32Handler& h);
  bool SetInt64Handler (const FieldDef* f,  const Int64Handler& h);
  bool SetUInt32Handler(const FieldDef* f, const UInt32Handler& h);
  bool SetUInt64Handler(const FieldDef* f, const UInt64Handler& h);
  bool SetFloatHandler (const FieldDef* f,  const FloatHandler& h);
  bool SetDoubleHandler(const FieldDef* f, const DoubleHandler& h);
  bool SetBoolHandler  (const FieldDef* f,   const BoolHandler& h);

  // Like the previous, but templated on the type on the value (ie. int32).
  // This is mostly useful to call from other templates.  To call this you must
  // specify the template parameter explicitly, ie:
  //   h->SetValueHandler<T>(f, UpbBind(MyHandler<T>, MyData));
  template <class T>
  bool SetValueHandler(
      const FieldDef *f,
      const typename ValueHandler<typename CanonicalType<T>::Type>::H& handler);

  // Sets handlers for a string field, which are defined as follows:
  //
  //   MySubClosure* startstr(MyClosure* c, const MyHandlerData* d,
  //                          size_t size_hint) {
  //     // Called when a string value begins.  The return value indicates the
  //     // closure for the string.  "size_hint" indicates the size of the
  //     // string if it is known, however if the string is length-delimited
  //     // and the end-of-string is not available size_hint will be zero.
  //     // This case is indistinguishable from the case where the size is
  //     // known to be zero.
  //     //
  //     // TODO(haberman): is it important to distinguish these cases?
  //     // If we had ssize_t as a type we could make -1 "unknown", but
  //     // ssize_t is POSIX (not ANSI) and therefore less portable.
  //     // In practice I suspect it won't be important to distinguish.
  //     return closure;
  //   }
  //
  //   size_t str(MyClosure* closure, const MyHandlerData* d,
  //              const char *str, size_t len) {
  //     // Called for each buffer of string data; the multiple physical buffers
  //     // are all part of the same logical string.  The return value indicates
  //     // how many bytes were consumed.  If this number is less than "len",
  //     // this will also indicate that processing should be halted for now,
  //     // like returning false or UPB_BREAK from any other callback.  If
  //     // number is greater than "len", the excess bytes will be skipped over
  //     // and not passed to the callback.
  //     return len;
  //   }
  //
  //   bool endstr(MyClosure* c, const MyHandlerData* d) {
  //     // Called when a string value ends.  Return value indicates whether
  //     // processing should continue.
  //     return true;
  //   }
  bool SetStartStringHandler(const FieldDef* f, const StartStringHandler& h);
  bool SetStringHandler(const FieldDef* f, const StringHandler& h);
  bool SetEndStringHandler(const FieldDef* f, const EndFieldHandler& h);

  // Sets the startseq handler, which is defined as follows:
  //
  //   MySubClosure *startseq(MyClosure* c, const MyHandlerData* d) {
  //     // Called when a sequence (repeated field) begins.  The returned
  //     // pointer indicates the closure for the sequence (or UPB_BREAK
  //     // to interrupt processing).
  //     return closure;
  //   }
  //
  //   h->SetStartSequenceHandler(f, UpbBind(startseq, new MyHandlerData(...)));
  //
  // Returns "false" if "f" does not belong to this message or is not a
  // repeated field.
  bool SetStartSequenceHandler(const FieldDef* f, const StartFieldHandler& h);

  // Sets the startsubmsg handler for the given field, which is defined as
  // follows:
  //
  //   MySubClosure* startsubmsg(MyClosure* c, const MyHandlerData* d) {
  //     // Called when a submessage begins.  The returned pointer indicates the
  //     // closure for the sequence (or UPB_BREAK to interrupt processing).
  //     return closure;
  //   }
  //
  //   h->SetStartSubMessageHandler(f, UpbBind(startsubmsg,
  //                                           new MyHandlerData(...)));
  //
  // Returns "false" if "f" does not belong to this message or is not a
  // submessage/group field.
  bool SetStartSubMessageHandler(const FieldDef* f, const StartFieldHandler& h);

  // Sets the endsubmsg handler for the given field, which is defined as
  // follows:
  //
  //   bool endsubmsg(MyClosure* c, const MyHandlerData* d) {
  //     // Called when a submessage ends.  Returns true to continue processing.
  //     return true;
  //   }
  //
  // Returns "false" if "f" does not belong to this message or is not a
  // submessage/group field.
  bool SetEndSubMessageHandler(const FieldDef *f, const EndFieldHandler &h);

  // Starts the endsubseq handler for the given field, which is defined as
  // follows:
  //
  //   bool endseq(MyClosure* c, const MyHandlerData* d) {
  //     // Called when a sequence ends.  Returns true continue processing.
  //     return true;
  //   }
  //
  // Returns "false" if "f" does not belong to this message or is not a
  // repeated field.
  bool SetEndSequenceHandler(const FieldDef* f, const EndFieldHandler& h);

  // Sets or gets the object that specifies handlers for the given field, which
  // must be a submessage or group.  Returns NULL if no handlers are set.
  bool SetSubHandlers(const FieldDef* f, const Handlers* sub);
  const Handlers* GetSubHandlers(const FieldDef* f) const;

  // Equivalent to GetSubHandlers, but takes the STARTSUBMSG selector for the
  // field.
  const Handlers* GetSubHandlers(Selector startsubmsg) const;

  // A selector refers to a specific field handler in the Handlers object
  // (for example: the STARTSUBMSG handler for field "field15").
  // On success, returns true and stores the selector in "s".
  // If the FieldDef or Type are invalid, returns false.
  // The returned selector is ONLY valid for Handlers whose MessageDef
  // contains this FieldDef.
  static bool GetSelector(const FieldDef* f, Type type, Selector* s);

  // Given a START selector of any kind, returns the corresponding END selector.
  static Selector GetEndSelector(Selector start_selector);

  // Returns the function pointer for this handler.  It is the client's
  // responsibility to cast to the correct function type before calling it.
  GenericFunction* GetHandler(Selector selector);

  // Returns the handler data that was registered with this handler.
  const void* GetHandlerData(Selector selector);

  // Could add any of the following functions as-needed, with some minor
  // implementation changes:
  //
  // const FieldDef* GetFieldDef(Selector selector);
  // static bool IsSequence(Selector selector);

 private:
  UPB_DISALLOW_POD_OPS(Handlers, upb::Handlers);

  friend UPB_INLINE GenericFunction *::upb_handlers_gethandler(
      const upb_handlers *h, upb_selector_t s);
  friend UPB_INLINE const void *::upb_handlers_gethandlerdata(
      const upb_handlers *h, upb_selector_t s);

#else
struct upb_handlers {
#endif
  upb_refcounted base;
  const upb_msgdef *msg;
  const upb_handlers **sub;
  upb_status status_;  // Used only when mutable.
  upb_handlers_tabent table[1];  // Dynamically-sized field handler array.
};


#ifdef __cplusplus

namespace upb {

// Convenience macros for creating a Handler object that is wrapped with a
// type-safe wrapper function that converts the "void*" parameters/returns
// of the underlying C API into nice C++ function.
//
// Sample usage:
//   void OnValue1(MyClosure* c, const MyHandlerData* d, int32_t val) {
//     // do stuff ...
//   }
//
//   // Handler that doesn't need any data bound to it.
//   void OnValue2(MyClosure* c, int32_t val) {
//     // do stuff ...
//   }
//
//   // Handler that returns bool so it can return failure if necessary.
//   bool OnValue3(MyClosure* c, int32_t val) {
//     // do stuff ...
//     return ok;
//   }
//
//   // Member function handler.
//   class MyClosure {
//    public:
//     void OnValue(int32_t val) {
//       // do stuff ...
//     }
//   };
//
//   // Takes ownership of the MyHandlerData.
//   handlers->SetInt32Handler(f1, UpbBind(OnValue1, new MyHandlerData(...)));
//   handlers->SetInt32Handler(f2, UpbMakeHandler(OnValue2));
//   handlers->SetInt32Handler(f1, UpbMakeHandler(OnValue3));
//   handlers->SetInt32Handler(f2, UpbMakeHandler(&MyClosure::OnValue));

#ifdef UPB_CXX11

// In C++11, the "template" disambiguator can appear even outside templates,
// so all calls can safely use this pair of macros.

#define UpbMakeHandler(f) upb::MatchFunc(f).template GetFunc<f>()

// We have to be careful to only evaluate "d" once.
#define UpbBind(f, d) upb::MatchFunc(f).template GetFunc<f>((d))

#else

// Prior to C++11, the "template" disambiguator may only appear inside a
// template, so the regular macro must not use "template"

#define UpbMakeHandler(f) upb::MatchFunc(f).GetFunc<f>()

#define UpbBind(f, d) upb::MatchFunc(f).GetFunc<f>((d))

#endif  // UPB_CXX11

// This macro must be used in C++98 for calls from inside a template.  But we
// define this variant in all cases; code that wants to be compatible with both
// C++98 and C++11 should always use this macro when calling from a template.
#define UpbMakeHandlerT(f) upb::MatchFunc(f).template GetFunc<f>()

// We have to be careful to only evaluate "d" once.
#define UpbBindT(f, d) upb::MatchFunc(f).template GetFunc<f>((d))

// Handler: a struct that contains the (handler, data, deleter) tuple that is
// used to register all handlers.  Users can Make() these directly but it's
// more convenient to use the UpbMakeHandler/UpbBind macros above.
template <class T> class Handler {
 public:
  // The underlying, handler function signature that upb uses internally.
  typedef T FuncPtr;

  // Intentionally implicit.
  template <class F> Handler(F func);
  ~Handler();

 private:
  UPB_DISALLOW_COPY_AND_ASSIGN(Handler);
  friend class Handlers;
  FuncPtr handler_;
  mutable HandlerAttributes attr_;
  mutable bool registered_;
};

}  // namespace upb

extern "C" {
#endif  // __cplusplus

// Native C API.

// Handler function typedefs.
typedef bool upb_startmsg_handlerfunc(void *c, const void*);
typedef bool upb_endmsg_handlerfunc(void *c, const void *, upb_status *status);
typedef void* upb_startfield_handlerfunc(void *c, const void *hd);
typedef bool upb_endfield_handlerfunc(void *c, const void *hd);
typedef bool upb_int32_handlerfunc(void *c, const void *hd, int32_t val);
typedef bool upb_int64_handlerfunc(void *c, const void *hd, int64_t val);
typedef bool upb_uint32_handlerfunc(void *c, const void *hd, uint32_t val);
typedef bool upb_uint64_handlerfunc(void *c, const void *hd, uint64_t val);
typedef bool upb_float_handlerfunc(void *c, const void *hd, float val);
typedef bool upb_double_handlerfunc(void *c, const void *hd, double val);
typedef bool upb_bool_handlerfunc(void *c, const void *hd, bool val);
typedef void *upb_startstr_handlerfunc(void *c, const void *hd,
                                       size_t size_hint);
typedef size_t upb_string_handlerfunc(void *c, const void *hd, const char *buf,
                                      size_t n);
// upb_handlerattr
#define UPB_HANDLERATTR_INITIALIZER {NULL, NULL}

void upb_handlerattr_init(upb_handlerattr *attr);
void upb_handlerattr_uninit(upb_handlerattr *attr);

bool upb_handlerattr_sethandlerdata(upb_handlerattr *attr, void *hd,
                                    upb_handlerfree *cleanup);
UPB_INLINE const void *upb_handlerattr_handlerdata(
    const upb_handlerattr *attr) {
  return attr->handler_data_;
}

typedef void upb_handlers_callback(void *closure, upb_handlers *h);

// upb_handlers
upb_handlers *upb_handlers_new(const upb_msgdef *m,
                               const void *owner);
const upb_handlers *upb_handlers_newfrozen(const upb_msgdef *m,
                                           const void *owner,
                                           upb_handlers_callback *callback,
                                           void *closure);

// From upb_refcounted.
bool upb_handlers_isfrozen(const upb_handlers *h);
void upb_handlers_ref(const upb_handlers *h, const void *owner);
void upb_handlers_unref(const upb_handlers *h, const void *owner);
void upb_handlers_donateref(const upb_handlers *h, const void *from,
                            const void *to);
void upb_handlers_checkref(const upb_handlers *h, const void *owner);

const upb_status *upb_handlers_status(upb_handlers *h);
void upb_handlers_clearerr(upb_handlers *h);
const upb_msgdef *upb_handlers_msgdef(const upb_handlers *h);

bool upb_handlers_setstartmsg(upb_handlers *h, upb_startmsg_handlerfunc *func,
                              upb_handlerattr *attr);
bool upb_handlers_setendmsg(upb_handlers *h, upb_endmsg_handlerfunc *func,
                            upb_handlerattr *attr);
bool upb_handlers_setint32(upb_handlers *h, const upb_fielddef *f,
                           upb_int32_handlerfunc *func, upb_handlerattr *attr);
bool upb_handlers_setint64(upb_handlers *h, const upb_fielddef *f,
                           upb_int64_handlerfunc *func, upb_handlerattr *attr);
bool upb_handlers_setuint32(upb_handlers *h, const upb_fielddef *f,
                            upb_uint32_handlerfunc *func,
                            upb_handlerattr *attr);
bool upb_handlers_setuint64(upb_handlers *h, const upb_fielddef *f,
                            upb_uint64_handlerfunc *func,
                            upb_handlerattr *attr);
bool upb_handlers_setfloat(upb_handlers *h, const upb_fielddef *f,
                           upb_float_handlerfunc *func, upb_handlerattr *attr);
bool upb_handlers_setdouble(upb_handlers *h, const upb_fielddef *f,
                            upb_double_handlerfunc *func,
                            upb_handlerattr *attr);
bool upb_handlers_setbool(upb_handlers *h, const upb_fielddef *f,
                          upb_bool_handlerfunc *func,
                          upb_handlerattr *attr);
bool upb_handlers_setstartstr(upb_handlers *h, const upb_fielddef *f,
                              upb_startstr_handlerfunc *func,
                              upb_handlerattr *attr);
bool upb_handlers_setstring(upb_handlers *h, const upb_fielddef *f,
                            upb_string_handlerfunc *func,
                            upb_handlerattr *attr);
bool upb_handlers_setendstr(upb_handlers *h, const upb_fielddef *f,
                            upb_endfield_handlerfunc *func,
                            upb_handlerattr *attr);
bool upb_handlers_setstartseq(upb_handlers *h, const upb_fielddef *f,
                              upb_startfield_handlerfunc *func,
                              upb_handlerattr *attr);
bool upb_handlers_setstartsubmsg(upb_handlers *h, const upb_fielddef *f,
                                 upb_startfield_handlerfunc *func,
                                 upb_handlerattr *attr);
bool upb_handlers_setendsubmsg(upb_handlers *h, const upb_fielddef *f,
                               upb_endfield_handlerfunc *func,
                               upb_handlerattr *attr);
bool upb_handlers_setendseq(upb_handlers *h, const upb_fielddef *f,
                            upb_endfield_handlerfunc *func,
                            upb_handlerattr *attr);

bool upb_handlers_setsubhandlers(upb_handlers *h, const upb_fielddef *f,
                                 const upb_handlers *sub);
const upb_handlers *upb_handlers_getsubhandlers(const upb_handlers *h,
                                                const upb_fielddef *f);
const upb_handlers *upb_handlers_getsubhandlers_sel(const upb_handlers *h,
                                                    upb_selector_t sel);

UPB_INLINE upb_func *upb_handlers_gethandler(const upb_handlers *h,
                                             upb_selector_t s) {
  return (upb_func *)h->table[s].func;
}

UPB_INLINE const void *upb_handlers_gethandlerdata(const upb_handlers *h,
                                                   upb_selector_t s) {
  return upb_handlerattr_handlerdata(&h->table[s].attr);
}

// Handler types for single fields.
// Right now we only have one for TYPE_BYTES but ones for other types
// should follow.
//
// These follow the same handlers protocol for fields of a message.
typedef struct { upb_handlers_tabent table[3]; } upb_byteshandler;

void upb_byteshandler_init(upb_byteshandler *h);
void upb_byteshandler_uninit(upb_byteshandler *h);

// Caller must ensure that "d" outlives the handlers.
// TODO(haberman): support handlerfree function for the data.
// TODO(haberman): should this have a "freeze" operation?  It's not necessary
// for memory management, but could be useful to force immutability and provide
// a convenient moment to verify that all registration succeeded.
bool upb_byteshandler_setstartstr(upb_byteshandler *h,
                                  upb_startstr_handlerfunc *func, void *d);
bool upb_byteshandler_setstring(upb_byteshandler *h,
                                upb_string_handlerfunc *func, void *d);
bool upb_byteshandler_setendstr(upb_byteshandler *h,
                                upb_endfield_handlerfunc *func, void *d);

#ifdef __cplusplus
namespace upb {
typedef upb_byteshandler  BytesHandler;
}
#endif

// "Static" methods
bool upb_handlers_freeze(upb_handlers *const *handlers, int n, upb_status *s);
upb_handlertype_t upb_handlers_getprimitivehandlertype(const upb_fielddef *f);
bool upb_handlers_getselector(const upb_fielddef *f, upb_handlertype_t type,
                              upb_selector_t *s);
UPB_INLINE upb_selector_t upb_handlers_getendselector(upb_selector_t start) {
  return start + 1;
}

// Internal-only.
uint32_t upb_handlers_selectorbaseoffset(const upb_fielddef *f);
uint32_t upb_handlers_selectorcount(const upb_fielddef *f);
#ifdef __cplusplus
}  // extern "C"
#endif

#include "upb/handlers-inl.h"

#endif  // UPB_HANDLERS_H
